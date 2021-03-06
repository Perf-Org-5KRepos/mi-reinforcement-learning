/*!
 * Copyright (C) tkornuta, IBM Corporation 2015-2019
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*!
 * \file GridworldDeepQLearning.cpp
 * \brief 
 * \author tkornut
 * \date Apr 21, 2016
 */

#include <limits>
#include <utils/RandomGenerator.hpp>
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
		step_reward("step_reward", 0.0),
		discount_rate("discount_rate", 0.9),
		learning_rate("learning_rate", 0.1),
		epsilon("epsilon", 0.1),
		statistics_filename("statistics_filename","dql_statistics.csv"),
		mlnn_filename("mlnn_filename", "dql_mlnn.txt"),
		mlnn_save("mlnn_save", false),
		mlnn_load("mlnn_load", false)
	{
	// Register properties - so their values can be overridden (read from the configuration file).
	registerProperty(step_reward);
	registerProperty(discount_rate);
	registerProperty(learning_rate);
	registerProperty(epsilon);
	registerProperty(statistics_filename);
	registerProperty(mlnn_filename);
	registerProperty(mlnn_save);
	registerProperty(mlnn_load);

	LOG(LINFO) << "Properties registered";
}


GridworldDeepQLearning::~GridworldDeepQLearning() {
	delete(w_chart);
}


void GridworldDeepQLearning::initialize(int argc, char* argv[]) {
	// Initialize GLUT! :]
	VGL_MANAGER->initializeGLUT(argc, argv);

	collector_ptr = std::make_shared < mic::utils::DataCollector<std::string, float> >( );
	// Add containers to collector.
	collector_ptr->createContainer("number_of_steps",  mic::types::color_rgba(255, 0, 0, 180));
	collector_ptr->createContainer("average_number_of_steps", mic::types::color_rgba(255, 255, 0, 180));
	collector_ptr->createContainer("collected_reward", mic::types::color_rgba(0, 255, 0, 180));
	collector_ptr->createContainer("average_collected_reward", mic::types::color_rgba(0, 255, 255, 180));

	sum_of_iterations = 0;
	sum_of_rewards = 0;

	// Create the visualization windows - must be created in the same, main thread :]
	w_chart = new WindowCollectorChart<float>("GridworldDeepQLearning", 256, 256, 0, 0);
	w_chart->setDataCollectorPtr(collector_ptr);

}

void GridworldDeepQLearning::initializePropertyDependentVariables() {
	// Initialize the gridworld.
	grid_env.initializeEnvironment();

	// Try to load neural network from file.
	if ((mlnn_load) && (neural_net.load(mlnn_filename))) {
		// Do nothing ;)
	} else {
		// Create a simple neural network.
		// gridworld wxhx4 -> 100 -> 4 -> regression!; batch size is set to one.
		neural_net.pushLayer(new Linear<float>((size_t) grid_env.getEnvironmentWidth() * grid_env.getEnvironmentHeight(), 250));
		neural_net.pushLayer(new ReLU<float>(250));
		neural_net.pushLayer(new Linear<float>(250, 100));
		neural_net.pushLayer(new ReLU<float>(100));
		neural_net.pushLayer(new Linear<float>(100, 4));

		// Set batch size to 1.
		//neural_net.resizeBatch(1);
		// Change optimization function from default GradientDescent to Adam.
		neural_net.setOptimization<mic::neural_nets::optimization::Adam<float> >();
		// Set loss function -> regression!
		neural_net.setLoss <mic::neural_nets::loss::SquaredErrorLoss<float> >();

		LOG(LINFO) << "Generated new neural network";
	}//: else
}


void GridworldDeepQLearning::startNewEpisode() {
	LOG(LSTATUS) << "Starting new episode " << episode;

	// Generate the gridworld (and move player to initial position).
	grid_env.initializeEnvironment();

	LOG(LSTATUS) << "Network responses: \n" <<  streamNetworkResponseTable();
	LOG(LSTATUS) << "Environment: \n" << grid_env.environmentToString();

}


void GridworldDeepQLearning::finishCurrentEpisode() {
	LOG(LTRACE) << "End of the episode " << episode;

	float reward = grid_env.getStateReward(grid_env.getAgentPosition());
	sum_of_iterations += iteration;
	sum_of_rewards += reward;

	// Add variables to container.
	collector_ptr->addDataToContainer("number_of_steps",iteration);
	collector_ptr->addDataToContainer("average_number_of_steps",(float)sum_of_iterations/episode);
	collector_ptr->addDataToContainer("collected_reward", reward);
	collector_ptr->addDataToContainer("average_collected_reward", (float)sum_of_rewards/episode);

	// Export reward "convergence" diagram.
	collector_ptr->exportDataToCsv(statistics_filename);

	// Save nn to file.
	if (mlnn_save)
		neural_net.save(mlnn_filename);
}


std::string GridworldDeepQLearning::streamNetworkResponseTable() {
	LOG(LTRACE) << "streamNetworkResponseTable()";
	std::string rewards_table;
	std::string actions_table;

	// Remember the current state i.e. player position.
	mic::types::Position2D current_player_pos_t = grid_env.getAgentPosition();

	rewards_table += "Action values:\n";
	actions_table += "Best actions:\n";
	// Generate all possible states and all possible rewards.
	for (size_t y=0; y<grid_env.getEnvironmentHeight(); y++){
		rewards_table += "| ";
		actions_table += "| ";
		for (size_t x=0; x<grid_env.getEnvironmentWidth(); x++) {
			float bestqval = -std::numeric_limits<float>::infinity();
			size_t best_action = -1;

			// Check network response for given state.
			grid_env.moveAgentToPosition(Position2D(x,y));
			mic::types::MatrixXfPtr tmp_state = grid_env.encodeAgentGrid();
			//std::cout<< "tmp_state = " << tmp_state->transpose() << std::endl;
			// Pass the data and get predictions.
			neural_net.forward(tmp_state);
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
				if (grid_env.isStateAllowed(x,y) && (!grid_env.isStateTerminal(x,y)) && grid_env.isActionAllowed(x,y,a) && (qval > bestqval)){
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
	grid_env.moveAgentToPosition(current_player_pos_t);

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

	for(mic::types::NESWAction action : actions) {
		// .. and find the value of teh best allowed action.
		if(grid_env.isActionAllowed(action)) {
			float qvalue = pred[(size_t)action.getType()];
			if (qvalue > best_qvalue){
				best_qvalue = qvalue;
			}
		}//if is allowed
	}//: for

	return best_qvalue;
}

mic::types::MatrixXfPtr GridworldDeepQLearning::getPredictedRewardsForCurrentState() {
	// Encode the current state.
	mic::types::MatrixXfPtr encoded_state = grid_env.encodeAgentGrid();
	// Pass the data and get predictions.
	neural_net.forward(encoded_state);
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
		if(grid_env.isActionAllowed(mic::types::NESWAction((mic::types::NESW)a))) {
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
	LOG(LSTATUS) << "Episode "<< episode << ": step " << iteration << "";

	// TMP!
	double 	nn_weight_decay = 0;

	// Get player pos at time t.
	mic::types::Position2D player_pos_t= grid_env.getAgentPosition();

	// Encode the current state at time t.
	mic::types::MatrixXfPtr encoded_state_t = grid_env.encodeAgentGrid();

	// Get the prediced rewards at time t...
	MatrixXfPtr tmp_rewards_t = getPredictedRewardsForCurrentState();
	// ... but make a local copy!
	MatrixXfPtr predicted_rewards_t (new MatrixXf(*tmp_rewards_t));
	LOG(LINFO) << "Agent position at state t: " << player_pos_t;
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
	bool random = false;

	// Epsilon-greedy action selection.
	if (RAN_GEN->uniRandReal() > eps){
		// Select best action.
		action = selectBestActionForCurrentState();
	} else {
		// Random action.
		action = A_RANDOM;
		random = true;
	}//: if

	// Execute action - until success.
	if (!grid_env.moveAgent(action)) {
		// The move was not possible! Learn that as well.
		(*predicted_rewards_t)((size_t)action.getType(), 0) = step_reward;

	} else {
		// Ok, move performed, get rewards.

		// Get new state s(t+1).
		mic::types::Position2D player_pos_t_prim = grid_env.getAgentPosition();

		LOG(LINFO) << "Agent position at t+1: " << player_pos_t_prim << " after performing the action = " << action << ((random) ? " [Random]" : "");

		// Check whether state t+1 is terminal.
		if(grid_env.isStateTerminal(player_pos_t_prim))
			(*predicted_rewards_t)((size_t)action.getType(), 0) = grid_env.getStateReward(player_pos_t_prim);
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
				(*predicted_rewards_t)((size_t)action.getType(), 0) = 5*r + discount_rate*max_q_st_prim_at_prim;;

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
	LOG(LSTATUS) << "Current environment: \n"  << grid_env.environmentToString();

	// Remember the previous position.
	player_pos_t_minus_prim = player_pos_t;
	// Check whether state t+1 is terminal - finish the episode.
	if(grid_env.isStateTerminal(grid_env.getAgentPosition()))
		return false;

	return true;
}


} /* namespace application */
} /* namespace mic */
