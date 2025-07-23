#include <ilcplex/ilocplex.h>
#include <ilcp/cp.h>
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        IloEnv env;
        IloModel model(env);

        // Example: Print CPLEX version (check linkage)
        std::cout << "CPLEX Concert C++ initialized. Version: " << CPX_VERSION << std::endl;

        // Your model will go here...

        env.end();
    } catch (IloException& e) {
        std::cerr << "Concert exception: " << e << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught." << std::endl;
        return 1;
    }
    return 0;
}
