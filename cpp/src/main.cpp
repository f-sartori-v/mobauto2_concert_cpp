#include <ilcplex/ilocplex.h>
#include <ilcp/cp.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

int main(int argc, char* argv[]) {
    // Check if I have the correct number of arguments
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " config.yaml demand.csv" << std::endl;
        return 1;
    }

    std::string config_path = argv[1];
    std::string demand_path = argv[2];

    // Read configuration file
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "Error opening configuration file: " << config_path << std::endl;
        return 1;
    }

    // Read demand file
    std::ifstream demand_file(demand_path);
    if (!demand_file.is_open()) {
        std::cerr << "Error opening demand file: " << demand_path << std::endl;
        return 1;
    }

    // Build Concert CP Model
    IloEnv env;
    try
    {
        IloModel model(env);

        IloCP cp(model)
    }
    catch(IloException& e)
    {
        std::cerr << e.what() << '\n';
    }
    

    try {
        IloEnv env;
        IloModel model(env);

        // Example: Print CPLEX version (check linkage)
        std::cout << "CPLEX Concert C++ initialized. Version: " << CPX_VERSION << std::endl;

        // Your model will go here...

        env.end();
    } catch (IloException& e) {
        std::cerr << "Concert exception: " << e << std::endl;
        env.end();
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught." << std::endl;
        env.end();
        return 1;
    }
    env.end();
    return 0;
}
