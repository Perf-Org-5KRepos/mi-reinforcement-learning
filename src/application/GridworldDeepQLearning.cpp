/*!
 * \file GridworldDeepQLearning.cpp
 * \brief 
 * \author tkornut
 * \date Apr 21, 2016
 */

#include <limits>
#include <data_utils/RandomGenerator.hpp>
#include <application/GridworldDeepQLearning.hpp>

namespace mic {
namespace application {

/*!
 * \brief Registers the application.
 * \author tkornuta
 */
void RegisterApplication (void) {
	REGISTER_APPLICATION(mic::application::GridworldDeepQLearning);
}


GridworldDeepQLearning::GridworldDeepQLearning(std::string node_name_) : OpenGLEpisodicApplication(node_name_),
		gridworld_type("gridworld_type", 0),
		width("width", 4),
		height("height", 4),
		step_reward("step_reward", 0.0),
		discount_rate("discount_rate", 0.9),
		learning_rate("learning_rate", 0.1),
		epsilon("epsilon", 0.1),
		statistics_filename("statistics_filename","statistics_filename.csv")
	{
	// Register properties - so their values can be overridden (read from the configuration file).
	registerProperty(gridworld_type);
	registerProperty(width);
	registerProperty(step_reward);
	registerProperty(discount_rate);
	registerProperty(learning_rate);
	registerProperty(epsilon);
	registerProperty(statistics_filename);

	LOG(LINFO) << "Properties registered";
}


GridworldDeepQLearning::~GridworldDeepQLearning() {

}


void GridworldDeepQLearning::initialize(int argc, char* argv[]) {
	// Initialize GLUT! :]
	VGL_MANAGER->initializeGLUT(argc, argv);

	collector_ptr = std::make_shared < mic::data_io::DataCollector<std::string, float> >( );
	// Add containers to collector.
	collector_ptr->createContainer("number_of_steps",  mic::types::color_rgba(255, 0, 0, 180));
	collector_ptr->createContainer("average_number_of_steps", mic::types::color_rgba(0, 255, 0, 180));
	collector_ptr->createContainer("collected_reward", mic::types::color_rgba(0, 0, 255, 180));

	sum_of_iterations = 0;

	// Create the visualization windows - must be created in the same, main thread :]
	w_chart = new WindowFloatCollectorChart("GridworldDeepQLearning", 256, 256, 0, 0);
	w_chart->setDataCollectorPtr(collector_ptr);

}

void GridworldDeepQLearning::initializePropertyDependentVariables() {
	// Generate the gridworld.
	state.generateGridworld(gridworld_type, width, height);

	// Get width and height.
	width = state.getWidth();
	height = state.getHeight();

	// Create a simple neural network.
	// gridworld wxhx4 -> 100 -> 4 -> regression!; batch size is set to one.
	neural_net.addLayer(new Linear((size_t) width * height , 250, 1));
	neural_net.addLayer(new ReLU(250, 250, 1));
	neural_net.addLayer(new Linear(250, 100, 1));
	neural_net.addLayer(new ReLU(100, 100, 1));
	neural_net.addLayer(new Linear(100, 4, 1));
	neural_net.addLayer(new Regression(4, 4, 1));

}


void GridworldDeepQLearning::startNewEpisode() {
	LOG(LERROR) << "Start new episode";
	// Move player to start position.
	state.movePlayerToInitialPosition();

	LOG(LSTATUS) << "Network responses:" << std::endl << streamNetworkResponseTable();
	LOG(LSTATUS) << std::endl << state.streamGrid();

}


void GridworldDeepQLearning::finishCurrentEpisode() {
	LOG(LTRACE) << "End current episode";

	sum_of_iterations += iteration;

	// Add variables to container.
	collector_ptr->addDataToContainer("number_of_steps",iteration);
	collector_ptr->addDataToContainer("average_number_of_steps",(float)sum_of_iterations/episode);
	collector_ptr->addDataToContainer("collected_reward",state.getStateReward(state.getPlayerPosition()));

	// Export reward "convergence" diagram.
	collector_ptr->exportDataToCsv(statistics_filename);

}


bool GridworldDeepQLearning::move (mic::types::Action2DInterface ac_) {
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


std::string GridworldDeepQLearning::streamNetworkResponseTable() {
	LOG(LTRACE) << "streamNetworkResponseTable()";
	std::string rewards_table;
	std::string actions_table;

	// Remember the current state i.e. player position.
	mic::types::Position2D current_player_pos_t = state.getPlayerPosition();

	rewards_table += "Action values:\n";
	actions_table += "Best actions:\n";
	// Generate all possible states and all possible rewards.
	for (size_t y=0; y<height; y++){
		rewards_table += "| ";
		actions_table += "| ";
		for (size_t x=0; x<width; x++) {
			float bestqval = -std::numeric_limits<float>::infinity();
			size_t best_action = -1;

			// Check network response for given state.
			state.movePlayerToPosition(Position2D(x,y));
			mic::types::MatrixXfPtr tmp_state = state.encodeGrid();
			//std::cout<< "tmp_state = " << tmp_state->transpose() << std::endl;
			// Pass the data and get predictions.
			neural_net.forward(*tmp_state);
			mic::types::MatrixXfPtr tmp_predicted_rewards = neural_net.getPredictions();
			float*  qstate = tmp_predicted_rewards->data();

			for (size_t a=0; a<4; a++) {
				float qval = qstate[a];

				rewards_table += std::to_string(qval);
				if (a==3)
					rewards_table += " | ";
				else
					rewards_table += " , ";

				// Remember the best value.
				if (qval > bestqval){
					bestqval = qval;
					best_action = a;
				}//: if

			}//: for a(ctions)
			switch(best_action){
				case 0 : actions_table += "N | "; break;
				case 1 : actions_table += "E | "; break;
				case 2 : actions_table += "S | "; break;
				case 3 : actions_table += "W | "; break;
				default: actions_table += "- | ";
			}//: switch

		}//: for x
		rewards_table += "\n";
		actions_table += "\n";
	}//: for y


	// Move player to previous position.
	state.movePlayerToPosition(current_player_pos_t);

	return rewards_table + actions_table;
}



float GridworldDeepQLearning::computeBestValueForCurrentState(){
	LOG(LTRACE) << "computeBestValue";
	float best_qvalue = -std::numeric_limits<float>::infinity();

	// Create a list of possible actions.
	std::vector<mic::types::NESWAction> actions;
	actions.push_back(A_NORTH);
	actions.push_back(A_EAST);
	actions.push_back(A_SOUTH);
	actions.push_back(A_WEST);

	// Check the results of actions one by one... (there is no need to create a separate copy of predictions)
	MatrixXfPtr predictions_sample = getPredictedRewardsForCurrentState();
	//LOG(LERROR) << "Selecting action from predictions:\n" << predictions_sample->transpose();
	float* pred = predictions_sample->data();

	for(size_t a=0; a<4; a++) {
		// Find the best action allowed.
		if(state.isActionAllowed(mic::types::NESWAction((mic::types::NESW)a))) {
			float qvalue = pred[a];
			if (qvalue > best_qvalue){
				best_qvalue = qvalue;
			}
		}//if is allowed
	}//: for

	return best_qvalue;
}

mic::types::MatrixXfPtr GridworldDeepQLearning::getPredictedRewardsForCurrentState() {
	// Encode the current state.
	mic::types::MatrixXfPtr encoded_state = state.encodeGrid();
	// Pass the data and get predictions.
	neural_net.forward(*encoded_state);
	// Return the predictions.
	return neural_net.getPredictions();
}


mic::types::NESWAction GridworldDeepQLearning::selectBestActionForCurrentState(){
	LOG(LTRACE) << "selectBestAction";

	// Greedy methods - returns the index of element with greatest value.
	mic::types::NESWAction best_action = A_RANDOM;
    float best_qvalue = -std::numeric_limits<float>::infinity();

	// Create a list of possible actions.
	std::vector<mic::types::NESWAction> actions;
	actions.push_back(A_NORTH);
	actions.push_back(A_EAST);
	actions.push_back(A_SOUTH);
	actions.push_back(A_WEST);

	// Check the results of actions one by one... (there is no need to create a separate copy of predictions)
	MatrixXfPtr predictions_sample = getPredictedRewardsForCurrentState();
	//LOG(LERROR) << "Selecting action from predictions:\n" << predictions_sample->transpose();
	float* pred = predictions_sample->data();

	for(size_t a=0; a<4; a++) {
		// Find the best action allowed.
		if(state.isActionAllowed(mic::types::NESWAction((mic::types::NESW)a))) {
			float qvalue = pred[a];
			if (qvalue > best_qvalue){
				best_qvalue = qvalue;
				best_action.setAction((mic::types::NESW)a);
			}
		}//if is allowed
	}//: for

	return best_action;
}

bool GridworldDeepQLearning::performSingleStep() {
	LOG(LERROR) << "Episode "<< episode << ": step " << iteration << "";

	// TMP!
	double 	nn_weight_decay = 0;

	// Get player pos at time t.
	mic::types::Position2D player_pos_t= state.getPlayerPosition();

	// Encode the current state at time t.
	mic::types::MatrixXfPtr encoded_state_t = state.encodeGrid();

	// Get the prediced rewards at time t...
	MatrixXfPtr tmp_rewards_t = getPredictedRewardsForCurrentState();
	// ... but make a local copy!
	MatrixXfPtr predicted_rewards_t (new MatrixXf(*tmp_rewards_t));
	LOG(LINFO) << "Player position at state t: " << player_pos_t;
	LOG(LSTATUS) << "Predicted rewards for state t: " << predicted_rewards_t->transpose();

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

	// Execute action - until success.
	if (!move(action)) {
		// The move was not possible! Learn that as well.
		(*predicted_rewards_t)((size_t)action.getType(), 0) = step_reward;

	} else {
		// Ok, move performed, get rewards.

		// Get new state s(t+1).
		mic::types::Position2D player_pos_t_prim = state.getPlayerPosition();

		LOG(LINFO) << "Player position at t+1: " << player_pos_t_prim << " after performing the action = " << action << " action index=" << (size_t)action.getType();

		// Check whether state t+1 is terminal.
		if(state.isStateTerminal(player_pos_t_prim))
			(*predicted_rewards_t)((size_t)action.getType(), 0) = state.getStateReward(player_pos_t_prim);
		else {
			// Update running average for given action - Deep Q learning!
			float r = step_reward;
			// Get best value for the NEXT state (!).
			float max_q_st_prim_at_prim = computeBestValueForCurrentState();

			LOG(LWARNING) << "step_reward = " << step_reward;
			LOG(LWARNING) << "max_q_st_prim_at_prim = " << max_q_st_prim_at_prim;

			// If next state best value is finite.
			if (std::isfinite(max_q_st_prim_at_prim))
				(*predicted_rewards_t)((size_t)action.getType(), 0) = r + discount_rate*max_q_st_prim_at_prim;
			else
				(*predicted_rewards_t)((size_t)action.getType(), 0) = r;

			// Special case - punish going back!
			if (player_pos_t_minus_prim == player_pos_t_prim)
				(*predicted_rewards_t)((size_t)action.getType(), 0) = step_reward;

		}//: else is terminal state
	}//: else !move


	// Deep Q learning - train network with the desired values.
	LOG(LERROR) << "Training with state: " << encoded_state_t->transpose();
	LOG(LERROR) << "Training with desired rewards: " << predicted_rewards_t->transpose();
	LOG(LSTATUS) << "Network responses before training:" << std::endl << streamNetworkResponseTable();

	// Train network with rewards.
	float loss = neural_net.train (encoded_state_t, predicted_rewards_t, learning_rate, nn_weight_decay);
	LOG(LSTATUS) << "Training loss:" << loss;

	LOG(LSTATUS) << "Network responses after training:" << std::endl << streamNetworkResponseTable();
	LOG(LSTATUS) << "The resulting state:" << std::endl << state.streamGrid();

	// Remember the previous position.
	player_pos_t_minus_prim = player_pos_t;
	// Check whether state t+1 is terminal - finish the episode.
	if(state.isStateTerminal(state.getPlayerPosition()))
		return false;

	return true;
}


} /* namespace application */
} /* namespace mic */
