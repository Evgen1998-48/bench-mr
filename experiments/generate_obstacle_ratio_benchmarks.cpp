#include <base/PolygonMaze.h>
#include <utils/Log.h>
#include "base/PlannerSettings.h"

#include <fstream>
#include <iostream>
#include <iomanip>

namespace og = ompl::geometric;

/**
 * Generate a JSON file with all the polygon maze environments.
 */
int main(int argc, char **argv) {
  nlohmann::json info;

  global::settings.env.type = "grid";
  global::settings.env.grid.width = 100;
  global::settings.env.grid.height = 100;
  global::settings.env.grid.generator = "random";
  global::settings.benchmark.runs = 5;
  Log::instantiateRun();
  for (int seed = 1; seed <= 5; ++seed) {
    for (double ratio : {0.01, 0.05, 0.08, 0.12}) {
      global::settings.env.grid.random.obstacle_ratio = ratio;
      global::settings.env.grid.seed = seed;
      global::settings.env.createEnvironment();

      global::settings.environment->to_json(info["environment"]);
      info["settings"] = nlohmann::json(global::settings)["settings"];
      Log::log(info);

      if (seed == 1) {
        // save benchmark configuration
          global::settings.benchmark.log_file =
                  "corridor_radius_" + std::to_string(ratio);
        std::ofstream o("../benchmarks/obstacle_ratio_" +
                        std::to_string(ratio) + ".json");
        o << std::setw(2) << nlohmann::json(global::settings);
        o.close();
      }
    }
  }

  Log::save("obstacle_ratios.json");

  return EXIT_SUCCESS;
}
