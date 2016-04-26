/*!
 * \file GridworldDRLExperienceReplay.cpp
 * \brief 
 * \author tkornut
 * \date Apr 26, 2016
 */

#include <limits>
#include <data_utils/RandomGenerator.hpp>
#include <application/GridworldDRLExperienceReplay.hpp>

namespace mic {
namespace application {

/*!
 * \brief Registers the application.
 * \author tkornuta
 */
void RegisterApplication (void) {
	REGISTER_APPLICATION(mic::application::GridworldDRLExperienceReplay);
}


GridworldDRLExperienceReplay::GridworldDRLExperienceReplay(std::string node_name_) : OpenGLEpisodicApplication(node_name_),
		gridworld_type("gridworld_type", 0),
		width("width", 4),
		height("height", 4),
		batch_size("batch_size",1),
		step_reward("step_reward", 0.0),
		discount_rate("discount_rate", 0.9),
		learning_rate("learning_rate", 0.1),
		move_noise("move_noise",0.2),
		epsilon("epsilon", 0.1),
		statistics_filename("statistics_filename","statistics_filename.csv"),
		experiences(100,1)
	{
	// Register properties - so their values can be overridden (read from the configuration file).
	registerProperty(gridworld_type);
	registerProperty(width);
	registerProperty(step_reward);
	registerProperty(discount_rate);
	registerProperty(learning_rate);
	registerProperty(move_noise);
	registerProperty(epsilon);
	registerProperty(statistics_filename);

	LOG(LINFO) << "Properties registered";
}


GridworldDRLExperienceReplay::~GridworldDRLExperienceReplay() {

}


void GridworldDRLExperienceReplay::initialize(int argc, char* argv[]) {
	// Initialize GLUT! :]
	VGL_MANAGER->initializeGLUT(argc, argv);

	collector_ptr = std::make_shared < mic::data_io::DataCollector<std::string, float> >( );
	// Add containers to collector.
	collector_ptr->createContainer("number_of_steps",  mic::types::color_rgba(255, 0, 0, 180));
	collector_ptr->createContainer("average_number_of_steps", mic::types::color_rgba(0, 255, 0, 180));
	collector_ptr->createContainer("collected_reward", mic::types::color_rgba(0, 0, 255, 180));

	sum_of_iterations = 0;

	// Create the visualization windows - must be created in the same, main thread :]
	w_chart = new WindowFloatCollectorChart("GridworldDRLExperienceReplay", 256, 256, 0, 0);
	w_chart->setDataCollectorPtr(collector_ptr);

}

void GridworldDRLExperienceReplay::initializePropertyDependentVariables() {
	// Generate the gridworld.
	state.generateGridworld(gridworld_type, width, height);

	// Get width and height.
	width = state.getWidth();
	height = state.getHeight();

	// Create a simple neural network.
	// gridworld wxhx4 -> 100 -> 4 -> regression!; batch size is set to one.
	neural_net.addLayer(new Linear((size_t) width * height , 250, batch_size));
	neural_net.addLayer(new ReLU(250, 250, batch_size));
	neural_net.addLayer(new Linear(250, 100, batch_size));
	neural_net.addLayer(new ReLU(100, 100, batch_size));
	neural_net.addLayer(new Linear(100, 4, batch_size));
	neural_net.addLayer(new Regression(4, 4, batch_size));
}


void GridworldDRLExperienceReplay::startNewEpisode() {
	LOG(LERROR) << "Start new episode";
	// Move player to start position.
	state.movePlayerToInitialPosition();

	LOG(LSTATUS) << "Network responses:" << std::endl << streamNetworkResponseTable();
	LOG(LSTATUS) << std::endl << state.streamGrid();

}


void GridworldDRLExperienceReplay::finishCurrentEpisode() {
	LOG(LTRACE) << "End current episode";

	sum_of_iterations += iteration;

	// Add variables to container.
	collector_ptr->addDataToContainer("number_of_steps",iteration);
	collector_ptr->addDataToContainer("average_number_of_steps",(float)sum_of_iterations/episode);
	collector_ptr->addDataToContainer("collected_reward",state.getStateReward(state.getPlayerPosition()));

/*		// Export reward "convergence" diagram.
		collector_ptr->exportDataToCsv(statistics_filename);*/

}


bool GridworldDRLExperienceReplay::move (mic::types::Action2DInterface ac_) {
//	LOG(LINFO) << "Current move = " << ac_;
	// Compute destination.
    mic::types::Position2D new_pos = state.getPlayerPosition() + ac_;

	// Check whether the state is allowed.
	if (!state.isStateAllowed(new_pos))
		return false;

	// Move player.
	state.movePlayerToPosition(new_pos);
	return true;
}


std::string GridworldDRLExperienceReplay::streamNetworkResponseTable() {
	std::ostringstream os;
	// Make a copy of current gridworld.
	Gridworld tmp_grid = state;
	MatrixXf best_vals (height, width);
	best_vals.setValue(-std::numeric_limits<float>::infinity());

	os << "All rewards:\n";
	// Generate all possible states and all possible rewards.
	for (size_t y=0; y<height; y++){
		os << "| ";
		for (size_t x=0; x<width; x++) {
			// Check network response for given state.
			tmp_grid.movePlayerToPosition(Position2D(x,y));
			mic::types::MatrixXfPtr tmp_state = tmp_grid.encodeGrid();
			//std::cout<< "tmp_state = " << tmp_state->transpose() << std::endl;
			// Pass the data and get predictions.
			neural_net.forward(*tmp_state);
			mic::types::MatrixXfPtr tmp_predicted_rewards = neural_net.getPredictions();
			float*  qstate = tmp_predicted_rewards->data();

			for (size_t a=0; a<4; a++) {
				os << qstate[a];
				if (a==3)
					os << " | ";
				else
					os << " , ";
				// Remember the best value.
				if (qstate[a] >= best_vals(y,x))
					best_vals(y,x) = qstate[a];

			}//: for a(ctions)
		}//: for x
		os << std::endl;
	}//: for y
/*	os << std::endl;

	os << "Best rewards:\n";
	// Stream only the biggerst states.
	for (size_t y=0; y<height; y++){
		os << "| ";
		for (size_t x=0; x<width; x++) {
			os << best_vals(y,x) << " | ";
		}//: for x
		os << std::endl;
	}//: for y*/

	return os.str();
}



float GridworldDRLExperienceReplay::computeBestValueForCurrentState(){
	LOG(LTRACE) << "computeBestValue";
	float best_qvalue = -std::numeric_limits<float>::infinity();

	// Create a list of possible actions.
	std::vector<mic::types::NESWAction> actions;
	actions.push_back(A_NORTH);
	actions.push_back(A_EAST);
	actions.push_back(A_SOUTH);
	actions.push_back(A_WEST);

	// Check the results of actions one by one... (there is no need to create a separate copy of predictions)
	float* pred = getPredictedRewardsForCurrentState()->data();

	for(mic::types::NESWAction action : actions) {
		// .. and find the value of teh best allowed action.
		if(state.isActionAllowed(action)) {
			float qvalue = pred[(size_t)action.getType()];
			if (qvalue > best_qvalue)
				best_qvalue = qvalue;
		}//if is allowed
	}//: for

	return best_qvalue;
}

mic::types::MatrixXfPtr GridworldDRLExperienceReplay::getPredictedRewardsForCurrentState() {
	// Encode the current state.
	mic::types::MatrixXfPtr encoded_state = state.encodeGrid();
	// Pass the data and get predictions.
	neural_net.forward(*encoded_state);
	// Return the predictions.
	return neural_net.getPredictions();
}

mic::types::NESWAction GridworldDRLExperienceReplay::selectBestActionForCurrentState(){
	LOG(LTRACE) << "selectBestAction";

	// Greedy methods - returns the index of element with greatest value.
	mic::types::NESWAction best_action = A_RANDOM;
    float best_qvalue = 0;

	// Create a list of possible actions.
	std::vector<mic::types::NESWAction> actions;
	actions.push_back(A_NORTH);
	actions.push_back(A_EAST);
	actions.push_back(A_SOUTH);
	actions.push_back(A_WEST);

	// Check the results of actions one by one... (there is no need to create a separate copy of predictions)
	float* pred = getPredictedRewardsForCurrentState()->data();

	for(mic::types::NESWAction action : actions) {
		// ... and find the best allowed.
		if(state.isActionAllowed(action)) {
			float qvalue = pred[(size_t)action.getType()];
			if (qvalue > best_qvalue){
				best_qvalue = qvalue;
				best_action = action;
			}
		}//if is allowed
	}//: for

	return best_action;
}

bool GridworldDRLExperienceReplay::performSingleStep() {
	LOG(LERROR) << "Episode "<< episode << ": step " << iteration << "";

	// TMP!
	double 	nn_learning_rate = 0.001;
	double 	nn_weight_decay = 0;

	// Get player pos at time t.
	mic::types::Position2D player_pos_t= state.getPlayerPosition();
	LOG(LINFO) << "Player position at state t: " << player_pos_t;

	// Select the action.
	mic::types::NESWAction action;
	//action = A_NORTH;
	double eps = (double)epsilon;
	if ((double)epsilon < 0)
		eps = 1.0/(1.0+sqrt(episode));
	if (eps < 0.1)
		eps = 0.1;

	LOG(LDEBUG) << "eps = " << eps;
	// Epsilon-greedy action selection.
	if (RAN_GEN->uniRandReal() > eps){
		// Select best action.
		action = selectBestActionForCurrentState();
	} else {
		// Random action.
		action = A_RANDOM;
	}//: if

	// Execute action - do not monitor the success.
	move(action);

	// Get new state s(t+1).
	mic::types::Position2D player_pos_t_prim = state.getPlayerPosition();
	LOG(LINFO) << "Player position at t+1: " << player_pos_t_prim << " after performing the action = " << action << " action index=" << (size_t)action.getType();

	// Collect the experience.
	GridworldExperiencePtr exp(new GridworldExperience(player_pos_t, action, player_pos_t_prim));
	// Create an empty matrix for rewards - this will be recalculated each time the experience will be replayed anyway.
	MatrixXfPtr rewards (new MatrixXf(width * height , batch_size));
	// Add experience to experience table.
	experiences.add(exp, rewards);


	// Deep Q learning - train network with random sample from the experience memory.
	if (experiences.size() >= batch_size) {
		GridworldExperienceSample ges = experiences.getRandomSample();

		LOG(LERROR) << "Training with state t  : " << ges.data()->s_t;
		LOG(LERROR) << "Training with action   : " << ges.data()->a_t;
		LOG(LERROR) << "Training with state t+1: " << ges.data()->s_t_prim;
//		LOG(LERROR) << "Training with desired rewards: " << predicted_rewards_t->transpose();
/*		LOG(LSTATUS) << "Network responses before training:" << std::endl << streamNetworkResponseTable();

		// Encode the current state at time t.
		mic::types::MatrixXfPtr encoded_state_t = state.encodeGrid();

		// Train network with rewards.
		float loss = neural_net.train (encoded_state_t, predicted_rewards_t, nn_learning_rate, nn_weight_decay);
		LOG(LSTATUS) << "Training loss:" << loss;

		LOG(LSTATUS) << "Network responses after training:" << std::endl << streamNetworkResponseTable();


		//LOG(LSTATUS) << "Network responses:" << std::endl << streamNetworkResponseTable();
		LOG(LSTATUS) << "The resulting state:" << std::endl << state.streamGrid();
*/
	}

	LOG(LSTATUS) << "Network responses:" << std::endl << streamNetworkResponseTable();
	LOG(LSTATUS) << std::endl << state.streamGrid();

	// Check whether state t+1 is terminal - finish the episode.
	if(state.isStateTerminal(state.getPlayerPosition()))
		return false;

	return true;
}


} /* namespace encoders */
} /* namespace mic */
