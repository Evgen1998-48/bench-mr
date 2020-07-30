#include "PlannerSettings.h"

#include <ompl/base/objectives/PathLengthOptimizationObjective.h>
#include <ompl/base/spaces/DubinsStateSpace.h>
#include <ompl/base/spaces/ReedsSheppStateSpace.h>
#include <ompl/base/spaces/SE2StateSpace.h>
#include <utils/OptimizationObjective.h>

#include <steering_functions/include/ompl_state_spaces/CurvatureStateSpace.hpp>

#include "GridMaze.h"
#include "PolygonMaze.h"
#include "steer_functions/POSQ/POSQStateSpace.h"

#ifdef G1_AVAILABLE
#include "steer_functions/G1Clothoid/ClothoidSteering.hpp"
#endif

PlannerSettings::GlobalSettings global::settings;

/**
 * Instrumented state space allows to measure time spent on computing the
 * steer function.
 */
template <typename StateSpaceT>
struct InstrumentedStateSpace : public StateSpaceT {
  using StateSpaceT::StateSpaceT;

  double distance(const ob::State *state1,
                  const ob::State *state2) const override {
    global::settings.ompl.state_space_timer.resume();
    double d = StateSpaceT::distance(state1, state2);
    global::settings.ompl.state_space_timer.stop();
    return d;
  }

  void interpolate(const ob::State *from, const ob::State *to, double t,
                   ob::State *state) const override {
    global::settings.ompl.state_space_timer.resume();
    StateSpaceT::interpolate(from, to, t, state);
    global::settings.ompl.state_space_timer.stop();
  }
};

void PlannerSettings::GlobalSettings::SteerSettings::initializeSteering()
    const {
  // Construct the robot state space in which we're planning.
  if (steering_type == Steering::STEER_TYPE_REEDS_SHEPP)
    global::settings.ompl.state_space =
        ob::StateSpacePtr(new InstrumentedStateSpace<ob::ReedsSheppStateSpace>(
            car_turning_radius));
  else if (steering_type == Steering::STEER_TYPE_POSQ)
    global::settings.ompl.state_space = ob::StateSpacePtr(new POSQStateSpace());
  else if (steering_type == Steering::STEER_TYPE_DUBINS)
    global::settings.ompl.state_space = ob::StateSpacePtr(
        new InstrumentedStateSpace<ob::DubinsStateSpace>(car_turning_radius));
  else if (steering_type == Steering::STEER_TYPE_LINEAR)
    global::settings.ompl.state_space =
        ob::StateSpacePtr(new InstrumentedStateSpace<ob::SE2StateSpace>);
  else if (steering_type == Steering::STEER_TYPE_CC_DUBINS)
    global::settings.ompl.state_space = ob::StateSpacePtr(
        new InstrumentedStateSpace<hc_cc_spaces::CCDubinsStateSpace>(
            hc_cc.kappa, hc_cc.sigma, sampling_resolution));
  else if (steering_type == Steering::STEER_TYPE_CC_REEDS_SHEPP)
    global::settings.ompl.state_space = ob::StateSpacePtr(
        new InstrumentedStateSpace<hc_cc_spaces::CCReedsSheppStateSpace>(
            hc_cc.kappa, hc_cc.sigma, sampling_resolution));
  else if (steering_type == Steering::STEER_TYPE_HC_REEDS_SHEPP)
    global::settings.ompl.state_space = ob::StateSpacePtr(
        new InstrumentedStateSpace<hc_cc_spaces::HCReedsSheppStateSpace>(
            hc_cc.kappa, hc_cc.sigma, sampling_resolution));
#ifdef G1_AVAILABLE
  else if (steering_type == Steering::STEER_TYPE_CLOTHOID)
    global::settings.ompl.state_space =
        ob::StateSpacePtr(new InstrumentedStateSpace<G1ClothoidStateSpace>());
#else
  else if (steering_type == Steering::STEER_TYPE_CLOTHOID) {
    OMPL_ERROR("G1 Clothoid steering is not available in this release!");
    OMPL_ERROR(
        "Select a steering type other than STEER_TYPE_CLOTHOID in the "
        "global::settings.");
    exit(1);
  }
#endif
  else {
    OMPL_ERROR(
        "Unknown steer function has been defined. The state space is invalid.");
    exit(1);
  }

  global::settings.ompl.state_space->as<ob::SE2StateSpace>()->setBounds(
      global::settings.environment->bounds());
  //  global::settings.ompl.state_space->setup();

  global::settings.ompl.space_info =
      std::make_shared<ompl::base::SpaceInformation>(
          global::settings.ompl.state_space);
  global::settings.ompl.objective = ompl::base::OptimizationObjectivePtr(
      new OptimizationObjective(global::settings.ompl.space_info));
  global::settings.ompl.objective->setCostThreshold(
      ob::Cost(global::settings.ompl.cost_threshold));

#ifdef DEBUG
  std::cout << "global::settings.ompl.state_space->hasDefaultProjection() ? "
            << std::boolalpha
            << global::settings.ompl.state_space->hasDefaultProjection()
            << std::endl;
#endif

  OMPL_INFORM("Initialized steer function %s.",
              Steering::to_string(steering_type).c_str());
}

void PlannerSettings::GlobalSettings::EnvironmentSettings::createEnvironment() {
  if (global::settings.environment) {
    delete global::settings.environment;
  }
  if (type.value() == "grid") {
    if (grid.generator.value() == "corridor") {
      global::settings.environment = GridMaze::createRandomCorridor(
          grid.width, grid.height, grid.corridor.radius, grid.corridor.branches,
          grid.seed);
    } else if (grid.generator.value() == "random") {
      global::settings.environment = GridMaze::createRandom(
          grid.width, grid.height, grid.random.obstacle_ratio, grid.seed);
    } else if (grid.generator.value() == "image") {
      global::settings.environment = GridMaze::createFromImage(
          grid.image.source, grid.image.occupancy_threshold,
          grid.image.desired_width, grid.image.desired_height);
      if (!global::settings.environment) {
        exit(1);
      }
      global::settings.env.grid.width = global::settings.environment->width();
      global::settings.env.grid.height = global::settings.environment->height();
    } else {
      OMPL_ERROR("Unknown grid environment generator \"%s\".",
                 grid.generator.value().c_str());
      exit(1);
    }
  } else if (type.value() == "polygon") {
    global::settings.environment = PolygonMaze::loadFromSvg(polygon.source);
  } else {
    OMPL_ERROR("Unknown environment type \"%s\".", type.value().c_str());
    exit(1);
  }
  collision.initializeCollisionModel();
}

void PlannerSettings::GlobalSettings::EnvironmentSettings::CollisionSettings::
    initializeCollisionModel() {
  // Load polygon for polygon-based collision checker if necessary
  if (collision_model == robot::ROBOT_POLYGON) {
    robot_shape = SvgPolygonLoader::load(robot_shape_source)[0];
    robot_shape.value().center();
    robot_shape.value().scale(global::settings.env.polygon.scaling);
    OMPL_INFORM("Loaded polygon robot model from %s with %d vertices.",
                robot_shape_source.value().c_str(),
                robot_shape.value().points.size());
    OMPL_INFORM("\tBounds: [%.2f %.2f] -- [%.2f %.2f]",
                robot_shape.value().min().x, robot_shape.value().min().y,
                robot_shape.value().max().x, robot_shape.value().max().y);
  }
}
