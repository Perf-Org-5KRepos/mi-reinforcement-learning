/*!
 * \file SimpleGridworld.hpp
 * \brief 
 * \author tkornut
 * \date Mar 17, 2016
 */

#ifndef SRC_APPLICATION_SIMPLEGRIDWORLD_HPP_
#define SRC_APPLICATION_SIMPLEGRIDWORLD_HPP_

#include <vector>
#include <string>

#include <types/TensorTypes.hpp>

#include <opengl/application/OpenGLApplication.hpp>
#include <opengl/visualization/WindowFloatCollectorChart.hpp>
using namespace mic::opengl::visualization;

#include <types/Position2D.hpp>

namespace mic {
namespace application {

/*!
 * \brief Gridworld channels
 * \author tkornuta
 */
enum class GridworldChannels : std::size_t
{
	Goal = 0, ///< Channel storing the goal(s)
	Pit = 1, ///< Channel storing the pits(s)
	Wall = 2, ///< Channel storing the walls(s)
	Player = 3 ///< Channel storing the player pose
};


/*!
 * \brief Class implementing a simple gridworld application with reinforcement learning used for solving the task.
 * \author tkornuta
 */
class SimpleGridworld: public mic::opengl::application::OpenGLApplication {
public:
	/*!
	 * Default Constructor. Sets the application/node name, default values of variables, initializes classifier etc.
	 * @param node_name_ Name of the application/node (in configuration file).
	 */
	SimpleGridworld(std::string node_name_ = "application");

	/*!
	 * Destructor.
	 */
	virtual ~SimpleGridworld();

protected:
	/*!
	 * Initializes all variables that are property-dependent.
	 */
	virtual void initializePropertyDependentVariables();

	/*!
	 * Method initializes GLUT and OpenGL windows.
	 * @param argc Number of application parameters.
	 * @param argv Array of application parameters.
	 */
	virtual void initialize(int argc, char* argv[]);

	/*!
	 * Performs single step of computations.
	 */
	virtual bool performSingleStep();

private:

	/// Window for displaying ???.
	WindowFloatCollectorChart* w_chart;

	/// Data collector.
	mic::data_io::DataCollectorPtr<std::string, float> collector_ptr;

	/// Tensor storing the 3D Gridworld (x + y + 4 "depth" channels representing: 0 - goals, 1 - pits, 2 - walls, 3 - player).
	mic::types::TensorXf gridworld;

	/// Tensor storing values for all states (gridworld w * h).
	mic::types::TensorXf state_value_table;

	/// Property: type of mgridworld:
	/// 0: the exemplary grid 4x3.
	/// 1: the classic cliff grid 5x3.
	/// 2: the classic discount grid 5x5.
	/// 3: the classic bridge grid 7x3.
	/// 4: the classic book grid 4x4.
	/// 5: the classic maze grid 4x4.
	/// -1 (or else): random grid - all items (wall, goal and pit, player) placed randomly
	mic::configuration::Property<short> gridworld_type;

	/// Property: width of gridworld.
	mic::configuration::Property<size_t> width;

	/// Property: height of gridworld.
	mic::configuration::Property<size_t> height;

	/// Property: height of gridworld.
	mic::types::Position2D initial_position;


	/*!
	 * Property: step rewared, i.e. reward received by performing each step (typically negative).
	 */
	mic::configuration::Property<float> step_reward;

	/*!
	 * Property: future discount factor (should be in range 0.0-1.0).
	 */
	mic::configuration::Property<float> discount_factor;

	/*!
	 * Property: move noise, determining gow often action results in unintended direction.
	 */
	mic::configuration::Property<float> move_noise;

	/// Property: name of the file to which the statistics will be exported.
	mic::configuration::Property<std::string> statistics_filename;

	/*!
	 * 	Method initializes the exemplary grid.
	 *
	 * [[' ',' ',' ',' '],
	 *  ['S',-10,' ',' '],
	 *  [' ','','#',' '],
	 *  [' ',' ',' ',10]]
	 */
	void initExemplaryGrid();

	/*!
	 * Initializes the classic cliff gridworld.
	 *
	 * [[' ',' ',' ',' ',' '],
	 *  ['S',' ',' ',' ',10],
	 *  [-100,-100, -100, -100, -100]]
	 */
	void initClassicCliffGrid();

	/*!
	 * Initializes the classic discount gridworld.
	 *
	 * [[' ',' ',' ',' ',' '],
	 *  [' ','#',' ',' ',' '],
	 *  [' ','#', 1,'#', 10],
	 *   ['S',' ',' ',' ',' '],
	 *   [-10,-10, -10, -10, -10]]
	 */
	void initDiscountGrid();

	/*!
	 * Initializes the classic discount gridworld.
	 *
	 * [[ '#',-100, -100, -100, -100, -100, '#'],
	 *  [   1, 'S',  ' ',  ' ',  ' ',  ' ',  10],
	 *  [ '#',-100, -100, -100, -100, -100, '#']]
	 */
	void initBridgeGrid();

	/*!
	 * Initializes the classic Book gridworld - example from Sutton&Barto book on RL.
	 *
	 * [[' ',' ',' ',+1],
	 *  [' ','#',' ',-1],
	 *  ['S',' ',' ',' ']]
	 */
	void initBookGrid();

	/*!
	 * Initializes the classic maze gridworld.
	 *
	 * [[' ',' ',' ',+1],
	 *  ['#','#',' ','#'],
	 *  [' ','#',' ',' '],
	 *  [' ','#','#',' '],
	 *  ['S',' ',' ',' ']]
	 */
	void initMazeGrid();

	/*!
	 * Generates a random grid of size (width x height).
	 */
	void initRandomGrid();

	/*!
	 * Returns the (flattened, i.e. 2D) grid of characters.
	 * @return Flattened grid of chars.
	 */
	mic::types::Tensor<char> flattenGrid();

	/*!
	 * Steams the current state of the gridworld.
	 * @return Ostream with description of the gridworld.
	 */
	std::string streamGrid();


	/*!
	 * Steams the current state of the state-action values.
	 * @return Ostream with description of the state-action table.
	 */
	std::string streamStateActionTable();


	/*!
	 * Calculates the player position.
	 * @return Player position.
	 */
	mic::types::Position2D getPlayerPosition();

	/*!
	 * Move player to the position.
	 * @param pos_ Position to be checked.
	 * @param pos_ The position to be set.
	 */
	void movePlayerToPosition(mic::types::Position2D pos_);


	/*!
	 * Returns the reward associated with the given state.
	 * @param pos_ Position (state).
	 * @return Reward for being in given state (r).
	 */
	float getStateReward(mic::types::Position2D pos_);

	/*!
	 * Checks if position is allowed, i.e. within the gridworld boundaries and there is no wall at that place.
	 * @param pos_ Position to be checked.
	 * @return True if the position is allowed, false otherwise.
	 */
	bool isStateAllowed(mic::types::Position2D pos_);

	/*!
	 * Checks if position is terminal, i.e. player is standing in a pit or reached the goal. Returns reward associated with given state.
	 * @param pos_ Position (state) to be checked.
	 * @return The reward associated with "final" action (might be positive or negative), equal to zero means that the position is not final.
	 */
	bool isStateTerminal(mic::types::Position2D pos_);

	/*!
	 * Performs "deterministic" move. It is assumed that the move is truncated by the gridworld boundaries (no circular world assumption).
	 * @param ac_ The action to be performed.
	 * @return True if move was performed, false if it was not possible.
	 */
	bool move (mic::types::Action2DInterface ac_);

	/*!
	 * Calculates the Q-value, taking into consideration probabilistic transition between states (i.e. that north action can end up going east or west)
	 * @param pos_ Starting state (position).
	 * @param ac_ Action to be performed.
	 * @return Value ofr the function
	 */
	float computeQValueFromValues(mic::types::Position2D pos_, mic::types::NESWAction ac_);

	/*!
	 * Checks whether performing given action starting in given state is allowed.
	 * @param pos_ Starting state (position).
	 * @param ac_ Action to be performed.
	 * @return True if action is allowed, false otherwise.
	 */
	bool isActionAllowed(mic::types::Position2D pos_, mic::types::Action2DInterface ac_);

	/*!
	 * Calculates the best value for given state - by finding the action having the maximal expected value.
	 * @param pos_ Starting state (position).
	 * @return Value for given state.
	 */
	float computeBestValue(mic::types::Position2D pos_);


};


} /* namespace application */
} /* namespace mic */

#endif /* SRC_APPLICATION_SIMPLEGRIDWORLD_HPP_ */
