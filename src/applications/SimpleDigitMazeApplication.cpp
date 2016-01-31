/*!
 * \file TestApplication.cpp
 * \brief File contains definition of methods of simple exemplary application.
 * \author tkornut
 * \date Jan 27, 2016
 */

#include <applications/SimpleDigitMazeApplication.hpp>

#include <application/ApplicationFactory.hpp>

#include <random>
namespace mic {
namespace application {

/*!
 * \brief Registers application.
 * \author tkornuta
 */
void RegisterApplication (void) {
	REGISTER_APPLICATION(mic::applications::SimpleDigitMazeApplication);
}

} /* namespace application */

namespace applications {

SimpleDigitMazeApplication::SimpleDigitMazeApplication(std::string node_name_) : OpenGLApplication(node_name_),
		hidden_maze_number("hidden_maze", 0),
		hidden_x("hidden_x", 0),
		hidden_y("hidden_y", 0),
		hit_factor("hit_factor", 0.6),
		miss_factor("miss_factor", 0.2),
		action("action", -1)
	{
	// Register properties - so their values can be overridden (read from the configuration file).
	registerProperty(hidden_maze_number);
	registerProperty(hidden_x);
	registerProperty(hidden_y);
	registerProperty(hit_factor);
	registerProperty(miss_factor);
	registerProperty(action);

	LOG(LINFO) << "Properties registered";

	// Turn single step on.
	APP_STATE->pressSingleStep();

}


SimpleDigitMazeApplication::~SimpleDigitMazeApplication() {
	LOG(LINFO) << "Empty for now";
}


void SimpleDigitMazeApplication::initialize(int argc, char* argv[]) {
	LOG(LSTATUS) << "In here you should initialize Glut and create all OpenGL windows";

	// Initialize GLUT! :]
	VGL_MANAGER->initializeGLUT(argc, argv);

	// Create the visualization windows - must be created in the same, main thread :]
	w_chart = new WindowChart("Statistics", 256, 512, 0, 326);


}

void SimpleDigitMazeApplication::initializePropertyDependentVariables() {

	// Import mazes.
	if (!importer.importData())
		return;

	// Set problem dimensions.
	number_of_mazes = importer.getData().size();
	problem_dimensions = number_of_mazes * importer.maze_width * importer.maze_height;
	number_of_distinctive_patches = 10;

	// Create data containers.
	// Random device used for generation of colors.
	std::random_device rd;
	std::mt19937_64 rng_mt19937_64(rd());
	// Initialize uniform index distribution - integers.
	std::uniform_int_distribution<> color_dist(50, 200);
	// Create a single container for each maze.
	for (size_t m=0; m<5; m++) {
		std::string label = "P(m" + std::to_string(m) +")";
		int r= color_dist(rng_mt19937_64);
		int g= color_dist(rng_mt19937_64);
		int b= color_dist(rng_mt19937_64);
		std::cout << label << " g=" << r<< " g=" << r<< " b=" << b;

		w_chart->createDataContainer(label, mic::types::color_rgba(r, g, b, 180));
	}//: for

	// Set pointer to data.
	mazes = importer.getData();

	// Assign initial probabilities for all mazes/positions.
	for (size_t m=0; m<number_of_mazes; m++) {
		maze_probabilities.push_back((double) 1.0/ mazes.size());

		std::shared_ptr < Matrix<double> > position_probabilities(new Matrix <double> (importer.maze_height, importer.maze_width));
		for (size_t i=0; i<importer.maze_height; i++) {
			for (size_t j=0; j<importer.maze_width; j++) {
				(*position_probabilities)(i,j) = (double) 1.0/(problem_dimensions);
			}//: for j
		}//: for i

		maze_position_probabilities.push_back(position_probabilities);

		// Display results.
		LOG(LINFO) << (*mazes[m]);
		//LOG(LINFO) << (*maze_position_probabilities[m]);
	}//: for m


	// Collect statistics for all mazes - number of appearances of given "patch" (i.e. digit).
	for (size_t m=0; m<number_of_mazes; m++) {
		std::shared_ptr < Matrix<int> > maze = mazes[m];
		std::shared_ptr < mic::types::Vector<double> > patch_probabilities(new mic::types::Vector <double> (number_of_distinctive_patches));

		// Iterate through maze and collect occurences.
		for (size_t i=0; i<importer.maze_height; i++) {
			for (size_t j=0; j<importer.maze_width; j++) {
				short patch_id = (*maze)(i,j);
				(*patch_probabilities)(patch_id) += 1.0;
			}//: for j
		}//: for i

		// Divide by number of maze elements -> probabilities.
		for (size_t i=0; i<number_of_distinctive_patches; i++) {
			(*patch_probabilities)(i) /= number_of_mazes;
		}//: for

		//LOG(LINFO) << (*patch_probabilities);

		maze_patch_probabilities.push_back(patch_probabilities);
	}//: for m(azes)

	{ // Enter critical section - with the use of scoped lock from AppState!
		APP_DATA_SYNCHRONIZATION_SCOPED_LOCK();

		// Add data to chart window.
		for (size_t m=0; m<number_of_mazes; m++) {
			std::string label = "P(m" + std::to_string(m) +")";
			w_chart->addDataToContainer(label, maze_probabilities[m]);
		}//: for
	}//: end of critical section.

	LOG(LWARNING) << "Hidden position in maze " << hidden_maze_number << "= (" << hidden_y << "," << hidden_x << ")";
	// Get current observation.
	short obs =(*mazes[hidden_maze_number])(hidden_y, hidden_x);
	sense(obs);

	// Update maze_probabilities.
	for (size_t m=0; m<number_of_mazes; m++) {
		// Reset probability.
		maze_probabilities[m] = 0;
		std::shared_ptr < Matrix<double> > pos_probs = maze_position_probabilities[m];
		// Sum probabilities of all positions.
		for (size_t i=0; i<importer.maze_height; i++) {
			for (size_t j=0; j<importer.maze_width; j++) {
				maze_probabilities[m] += (*pos_probs)(i,j);
			}//: for j
		}//: for i
	}//: for m

	{ // Enter critical section - with the use of scoped lock from AppState!
		APP_DATA_SYNCHRONIZATION_SCOPED_LOCK();

		// Add data to chart window.
		for (size_t m=0; m<number_of_mazes; m++) {
			std::string label = "P(m" + std::to_string(m) +")";
			w_chart->addDataToContainer(label, maze_probabilities[m]);
		}//: for
	}//: end of critical section.

}


void SimpleDigitMazeApplication::sense (short obs_) {
	LOG(LINFO) << "Current observation=" << obs_;

	// Compute posterior distribution given Z (observation) - total probability.

	// For all mazes.
	double prob_sum = 0;
	for (size_t m=0; m<number_of_mazes; m++) {
		std::shared_ptr < Matrix<double> > pos_probs = maze_position_probabilities[m];
		std::shared_ptr < Matrix<int> > maze = mazes[m];

		// Display results.
/*		LOG(LERROR) << "Przed updatem";
		LOG(LERROR) << (*mazes[m]);
		LOG(LERROR) << (*maze_position_probabilities[m]);
*/

		// Iterate through position probabilities and update them.
		for (size_t y=0; y<importer.maze_height; y++) {
			for (size_t x=0; x<importer.maze_width; x++) {
				if ((*maze)(y,x) == obs_)
					(*pos_probs)(y,x) *= hit_factor;
				else
					(*pos_probs)(y,x) *= miss_factor;
				prob_sum += (*pos_probs)(y,x);
			}//: for j
		}//: for i
	}//: for m

	prob_sum = 1/prob_sum;
	// Normalize probabilities for all mazes.
	for (size_t m=0; m<number_of_mazes; m++) {
		std::shared_ptr < Matrix<double> > pos_probs = maze_position_probabilities[m];
		for (size_t i=0; i<importer.maze_height; i++) {
			for (size_t j=0; j<importer.maze_width; j++) {
				(*pos_probs)(i,j) *= prob_sum;
			}//: for j
		}//: for i

		// Display results.
		LOG(LDEBUG) << (*mazes[m]);
		LOG(LINFO) << (*maze_position_probabilities[m]);
	}//: for m

}

void SimpleDigitMazeApplication::move (mic::types::Action2DInterface ac_) {
	LOG(LERROR) << "Current move dy,dx= ( " << ac_.dy() << "," <<ac_.dx()<< ")";

	// For all mazes.
	for (size_t m=0; m<number_of_mazes; m++) {
		std::shared_ptr < Matrix<double> > pos_probs = maze_position_probabilities[m];
		Matrix<double> old_pose_probs = (*pos_probs);

/*		LOG(LERROR) << "Przed ruchem";
		LOG(LERROR) << (*mazes[m]);
		LOG(LERROR) << (*maze_position_probabilities[m]);*/

		// Iterate through position probabilities and update them.
		for (size_t y=0; y<importer.maze_height; y++) {
			for (size_t x=0; x<importer.maze_width; x++) {
				//std::cout << "i=" << i << " j=" << j << " dx=" << dx_ << " dy=" << dy_ << " (i - dx_) % 3 = " << (i +3 - dx_) % 3 << " (j - dy_) % 3=" << (j + 3 - dy_) % 3 << std::endl;
				(*pos_probs)((y + importer.maze_height +  ac_.dy()) %importer.maze_height, (x +importer.maze_width +  ac_.dx()) % importer.maze_width) = old_pose_probs(y, x);

			}//: for j
		}//: for i

		// Display results.
		LOG(LDEBUG) << (*mazes[m]);
		LOG(LWARNING) << (*maze_position_probabilities[m]);
	}//: for m

	// Perform the REAL move.
	hidden_y = (hidden_y + importer.maze_height +  ac_.dy()) % importer.maze_height;
	hidden_x = (hidden_x + importer.maze_width +  ac_.dx()) % importer.maze_width;

	LOG(LWARNING) << "Hidden position in maze " << hidden_maze_number << "= (" << hidden_y << "," << hidden_x << ")";

}

bool SimpleDigitMazeApplication::performSingleStep() {
	LOG(LWARNING) << "Perform a single step ";

	// Perform move.
	if (action == (short)-1)
		move(A_RANDOM);
	else
		move(mic::types::NESWAction((mic::types::NESW_action_type_t) (short)action));
	//move(A_EAST);

	// Get current observation.
	short obs =(*mazes[hidden_maze_number])(hidden_y, hidden_x);
	sense(obs);

	// Update maze_probabilities.
	for (size_t m=0; m<number_of_mazes; m++) {
		// Reset probability.
		maze_probabilities[m] = 0;
		std::shared_ptr < Matrix<double> > pos_probs = maze_position_probabilities[m];
		// Sum probabilities of all positions.
		for (size_t i=0; i<importer.maze_height; i++) {
			for (size_t j=0; j<importer.maze_width; j++) {
				maze_probabilities[m] += (*pos_probs)(i,j);
			}//: for j
		}//: for i
	}//: for m

	// Add data to chart window.
	for (size_t m=0; m<number_of_mazes; m++) {
		std::string label = "P(m" + std::to_string(m) +")";
		w_chart->addDataToContainer(label, maze_probabilities[m]);
	}//: for

	return true;
}






} /* namespace applications */
} /* namespace mic */
