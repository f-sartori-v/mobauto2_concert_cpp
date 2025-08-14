//#include <ilcplex/ilocplex.h>
#include <yaml-cpp/yaml.h>
#include <ilcp/cp.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>


struct DemandRequest {
    int req_id;
    std::string direction;
    double time;
};

std::vector<DemandRequest> load_demand_csv(const std::string& filename) {
    std::vector<DemandRequest> demands;
    std::ifstream file(filename);
    std::string line;

    std::getline(file, line);
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string cell;
        DemandRequest req;
        std::getline(ss, cell, ',');
        req.req_id = std::stoi(cell);
        std::getline(ss, cell, ',');
        req.direction = cell;
        std::getline(ss, cell, ',');
        req.time = std::stod(cell);
        demands.push_back(req);
    }
    return demands;
}

int main(int argc, char* argv[]) {
    // Check if I have the correct number of arguments
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " config.yaml demand.csv solution.json" << std::endl;
        return 1;
    }

    // Read configuration file
    std::string config_path = argv[1];
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "Error opening configuration file: " << config_path << std::endl;
        return 1;
    }

    // Read demand file
    std::string demand_path = argv[2];
    std::ifstream demand_file(demand_path);
    if (!demand_file.is_open()) {
        std::cerr << "Error opening demand file: " << demand_path << std::endl;
        return 1;
    }

    IloEnv env;
    int exit_code = 0;

    try {
        // Create the model
        IloModel model(env);

        // Load configuration
        YAML::Node config = YAML::LoadFile(config_path);
        
        // Extract parameters from the configuration
        YAML::Node time = config["time"];
        IloInt time_res = config["time"]["time_res"].as<int>();
        IloInt start_time = time["start_time"].as<int>() / time_res;
        IloInt end_time = time["end_time"].as<int>() / time_res;
        IloInt horizon = (end_time - start_time);

        YAML::Node fleet = config["fleet"];
        IloInt num_shuttles = fleet["num_shuttles"].as<int>();
        IloInt seat_capacity = fleet["seat_capacity"].as<int>();
        IloInt battery_capacity = fleet["battery_capacity_km"].as<int>();
        IloInt trip_distance = fleet["trip_distance_km"].as<int>();
        IloInt trip_duration = fleet["trip_duration_min"].as<int>() / time_res;
        IloInt min_recharge = fleet["min_partial_minutes"].as<int>() / time_res;
        IloInt max_recharge = fleet["min_full_minutes"].as<int>() / time_res;
        IloInt soc_threshold = fleet["soc_threshold"].as<int>();

        IloInt max_tasks = IloInt(round(double(horizon) / trip_duration));

        YAML::Node solver = config["solver"];
        int time_limit = solver["time_limit"].as<int>();
        std::string verbose = solver["log_verbosity"].as<std::string>();
        std::string search_type = solver["search_type"].as<std::string>();
        
        // Load demand requests
        std::vector<DemandRequest> demands = load_demand_csv(demand_path);
        std::cout << "Loaded " << demands.size() << " demand requests from " << demand_path << "." << std::endl;

        // Create variables for each shuttle and task
        std::vector<std::vector<std::map<std::string, IloIntervalVar>>> task_vars(num_shuttles);

        for (int i = 0; i < num_shuttles; ++i) {
            
            task_vars[i].resize(max_tasks);
            for (int j = 0; j < max_tasks; ++j) {

                std::string base = "shuttle_" + std::to_string(i) + "_to_task_" + std::to_string(j) + "_";
                
                // For each shuttle, create a master interval                
                IloIntervalVar shuttle_master(env);
                shuttle_master.setOptional();
                shuttle_master.setName((base + "S").c_str());
                task_vars[i][j]["S"] = shuttle_master;

                task_vars[i][j]["OUT"] = IloIntervalVar(env, trip_duration);
                task_vars[i][j]["OUT"].setOptional();
                task_vars[i][j]["OUT"].setStartMin(start_time);
                task_vars[i][j]["OUT"].setEndMax(end_time);
                task_vars[i][j]["OUT"].setName((base + "OUT").c_str());

                task_vars[i][j]["RET"] = IloIntervalVar(env, trip_duration);
                task_vars[i][j]["RET"].setOptional();
                task_vars[i][j]["RET"].setStartMin(start_time);
                task_vars[i][j]["RET"].setEndMax(end_time);
                task_vars[i][j]["RET"].setName((base + "RET").c_str());

                task_vars[i][j]["CRGp"] = IloIntervalVar(env, trip_duration * 2);
                task_vars[i][j]["CRGp"].setOptional();
                task_vars[i][j]["CRGp"].setStartMin(start_time);
                task_vars[i][j]["CRGp"].setEndMax(end_time);
                task_vars[i][j]["CRGp"].setName((base + "CRGp").c_str());

                task_vars[i][j]["CRGf"] = IloIntervalVar(env, trip_duration);
                task_vars[i][j]["CRGf"].setOptional();
                task_vars[i][j]["CRGf"].setStartMin(start_time);
                task_vars[i][j]["CRGf"].setEndMax(end_time);
                task_vars[i][j]["CRGf"].setName((base + "CRGf").c_str());

                task_vars[i][j]["END"] = IloIntervalVar(env);
                task_vars[i][j]["END"].setOptional();
                task_vars[i][j]["END"].setLengthMin(0);
                task_vars[i][j]["END"].setLengthMax(0);
                task_vars[i][j]["END"].setStartMin(start_time);
                task_vars[i][j]["END"].setEndMax(end_time);
                task_vars[i][j]["END"].setName((base + "END").c_str());

                // Constraint to force exactly one mode to be present for each slot
                IloIntervalVarArray alts(env);
                for (const auto& label0 : {"OUT", "RET", "CRGp", "CRGf", "END"})
                    alts.add(task_vars[i][j][label0]);
                model.add(IloAlternative(env, shuttle_master, alts));
            }

            // Ensure first task is OUT and last task is END
            task_vars[i][0]["OUT"].setPresent();
            task_vars[i][max_tasks - 1]["END"].setPresent();
        }

        // Logic Transition constraints
        for (int i = 0; i < num_shuttles; ++i) {
            for (int j = 0; j < max_tasks - 1; ++j) {
                        
                // Força: OUT só pode ser seguido de RET
                model.add(IloIfThen(env,
                    IloPresenceOf(env, task_vars[i][j]["OUT"]) == 1,
                    IloPresenceOf(env, task_vars[i][j+1]["RET"]) == 1));

                // RET pode ser seguido de OUT, CRGp, ou CRGf (pelo menos um presente)
                IloExpr sum_after_ret(env);
                for (const auto& label1 : {"OUT", "CRGp", "CRGf", "END"})
                    sum_after_ret += IloPresenceOf(env,task_vars[i][j+1][label1]);
                model.add(IloIfThen(env,
                    IloPresenceOf(env, task_vars[i][j]["RET"]) == 1,
                    sum_after_ret == 1));
                sum_after_ret.end();

                // CRGf só pode ser seguido de OUT ou outro CRGf
                IloExpr sum_after_crg(env);
                for (const auto& label2 : {"OUT", "CRGf"})
                    sum_after_crg += IloPresenceOf(env, task_vars[i][j+1][label2]);
                model.add(IloIfThen(env,
                    IloPresenceOf(env, task_vars[i][j]["CRGf"]) == 1,
                    sum_after_crg == 1));
                sum_after_crg.end();

                // CRGp só pode ser seguido de OUT
                model.add(IloIfThen(env,
                    IloPresenceOf(env, task_vars[i][j]["CRGp"]) == 1,
                    IloPresenceOf(env, task_vars[i][j+1]["OUT"]) == 1));

                // END só pode ser seguido de END
                model.add(IloIfThen(env,
                    IloPresenceOf(env, task_vars[i][j]["END"]) == 1,
                    IloPresenceOf(env, task_vars[i][j+1]["END"]) == 1));
            }
        }

        // Enforce chaining: if a task is present, the next task starts after the previous ends
        // Can be further reduced for scalability
        for (int i = 0; i < num_shuttles; ++i) {
            for (int j = 0; j < max_tasks - 1; ++j) {
                model.add(IloEndBeforeStart(env, task_vars[i][j]["S"], task_vars[i][j+1]["S"]));
            }
        }

        // Each shuttle's SOC starts at the battery capacity and is reduced by the trip distance
        // for each "OUT" and "RET" task, and increased by the recharge rate
        // for each "CRG" task, while ensuring that the SOC remains non-negative.
        // The SOC is initialized at the start time and must remain within the battery capacity.
        for (int i = 0; i < num_shuttles; ++i) {
            IloCumulFunctionExpr shuttle_soc(env);

            // Set initial SOC at the start of the horizon
            shuttle_soc += IloStep(env, start_time, battery_capacity);

            for (int j = 0; j < max_tasks; ++j) {
                shuttle_soc -= IloStepAtStart(task_vars[i][j]["OUT"], trip_distance);
                shuttle_soc -= IloStepAtStart(task_vars[i][j]["RET"], trip_distance);
                // Add the recharge rate for the length of CRG task:
                //double recharge_rate_p = (double)battery_capacity / (double)max_recharge;
                //double recharge_rate_f = (double)battery_capacity / (double)min_recharge;

                //shuttle_soc += IloPulse(task_vars[i][j]["CRGp"], recharge_rate_p);
                //shuttle_soc += IloPulse(task_vars[i][j]["CRGf"], recharge_rate_f);

                shuttle_soc += IloStepAtEnd(task_vars[i][j]["CRGp"], battery_capacity / 2);
                shuttle_soc += IloStepAtEnd(task_vars[i][j]["CRGf"], battery_capacity / 6);

                model.add(IloAlwaysIn(env, shuttle_soc, task_vars[i][j]["OUT"],
                              /*min*/ trip_distance,
                              /*max*/ battery_capacity));

                // Ensure SoC is below soc_threshold for CRGp
                model.add(IloAlwaysIn(env, shuttle_soc, task_vars[i][j]["CRGp"],
                                    /*min*/ 0,
                                    /*max*/ soc_threshold));
        }
            
            model.add(IloAlwaysIn(env, shuttle_soc, start_time, end_time, 0, battery_capacity));
            shuttle_soc.end();

        }


        // Create a 3D vector to hold the shuttle-task-demand variables
        std::vector<std::vector<std::vector<IloBoolVar>>> a_qid(
            num_shuttles,
            std::vector<std::vector<IloBoolVar>>(max_tasks, std::vector<IloBoolVar>(demands.size()))
        );

        for (int q = 0; q < num_shuttles; ++q) {
            for (int i = 0; i < max_tasks; ++i) {
                for (size_t d = 0; d < demands.size(); ++d) {
                    a_qid[q][i][d] = IloBoolVar(env, ("assign_shuttle" + std::to_string(q) + "_task" + std::to_string(i) + "_to_request" + std::to_string(d)).c_str());
                }
            }
        }

        for (int q = 0; q < num_shuttles; ++q) {
            for (int i = 0; i < max_tasks; ++i) {
                for (size_t d = 0; d < demands.size(); ++d) {
                    auto& req = demands[d];
                    IloBoolVar a = a_qid[q][i][d];
                    IloIntervalVar task;

                    if (req.direction == "OUTBOUND") {
                        task = task_vars[q][i]["OUT"];
                        model.add(IloIfThen(env, a == 1, IloPresenceOf(env, task) == 1));
                    }
                    if (req.direction == "RETURN") {
                        task = task_vars[q][i]["RET"];
                        model.add(IloIfThen(env, a == 1, IloPresenceOf(env, task) == 1));
                    }
                    // Time window constraint
                    int td = static_cast<int>(req.time);
                    int max_wait = 30 / time_res - 1;
                    model.add(IloIfThen(env, a == 1, IloStartOf(task) >= td));
                    model.add(IloIfThen(env, a == 1, IloStartOf(task) <= td + max_wait));
                }
            }
        }

        // Add constraints to ensure that each demand is assigned to at most one shuttle-task pair
        for (size_t d = 0; d < demands.size(); ++d) {
            IloExpr assign_sum(env);
            for (int q = 0; q < num_shuttles; ++q)
                for (int i = 0; i < max_tasks; ++i)
                    assign_sum += a_qid[q][i][d];
            model.add(assign_sum <= 1);
            assign_sum.end();
        }

        // Add constraints to ensure that each shuttle can only handle a maximum number of passengers
        // per task, which is equal to the seat capacity.
        for (int q = 0; q < num_shuttles; ++q) {
            for (int i = 0; i < max_tasks; ++i) {
                IloExpr pax(env);
                for (size_t d = 0; d < demands.size(); ++d)
                    pax += a_qid[q][i][d];
                model.add(pax <= seat_capacity);
                pax.end();
            }
        }


        // Symmetry breaking: ensure that the first shuttle starts no later than the subsequent shuttles
        for (int q = 0; q < num_shuttles - 1; ++q) {
            model.add(IloStartOf(task_vars[q][0]["S"]) <= IloStartOf(task_vars[q+1][0]["S"]));
        }

        // Set the objective function to minimize the total number of tasks, unmet demand and waiting time of served requests
        // Unmet flag and wait for each demand
        std::vector<IloBoolVar> u(demands.size());
        std::vector<IloIntVar> w(demands.size());

        // Trip flag for each shuttle/task slot
        std::vector<std::vector<IloBoolVar>> z(num_shuttles, std::vector<IloBoolVar>(max_tasks));

        for (size_t d = 0; d < demands.size(); ++d) {
            u[d] = IloBoolVar(env, ("u_" + std::to_string(d)).c_str());
            w[d] = IloIntVar(env, 0, IloInt(end_time-start_time), ("w_" + std::to_string(d)).c_str());
        }

        for (int q = 0; q < num_shuttles; ++q)
            for (int i = 0; i < max_tasks; ++i)
                z[q][i] = IloBoolVar(env, ("z_" + std::to_string(q) + "_task_" + std::to_string(i)).c_str());

        for (size_t d = 0; d < demands.size(); ++d) {
            IloExpr assign_sum(env);
            for (int q = 0; q < num_shuttles; ++q)
                for (int i = 0; i < max_tasks; ++i)
                    assign_sum += a_qid[q][i][d];
            // If unmet, assign_sum==0 => u[d]=1; if served, assign_sum==1 => u[d]=0
            model.add(assign_sum + u[d] == 1);
            assign_sum.end();
        }
            

        for (size_t d = 0; d < demands.size(); ++d) {
            IloExpr wait_sum(env);
            auto req = demands[d];
            for (int q = 0; q < num_shuttles; ++q) {
                for (int i = 0; i < max_tasks; ++i) {
                    IloIntervalVar task;
                    if (req.direction == "OUTBOUND") {
                        task = task_vars[q][i]["OUT"];
                    }
                    if (req.direction == "RETURN") {
                        task = task_vars[q][i]["RET"];
                    }
                    
                    wait_sum += a_qid[q][i][d] * (IloStartOf(task) - static_cast<int>(req.time));
                }
            }
            model.add(w[d] == wait_sum);
            wait_sum.end();
        }

        for (int q = 0; q < num_shuttles; ++q) {
            for (int i = 0; i < max_tasks; ++i) {
                model.add(z[q][i] >= IloPresenceOf(env, task_vars[q][i]["OUT"]));
                model.add(z[q][i] >= IloPresenceOf(env, task_vars[q][i]["RET"]));
                model.add(z[q][i] <= IloPresenceOf(env, task_vars[q][i]["OUT"]) + IloPresenceOf(env, task_vars[q][i]["RET"]));
            }
        }

        double alpha = 100;
        double gamma = 1000000;
        double delta = 1;

        IloExpr obj(env);
        for (size_t d = 0; d < demands.size(); ++d) {
            obj += alpha * w[d];
            obj += gamma * u[d];
        }
        
        for (int q = 0; q < num_shuttles; ++q)
            for (int i = 0; i < max_tasks; ++i)
                obj += delta * z[q][i];

        model.add(IloMinimize(env, obj));
        obj.end();

        IloIntervalVarArray all_intervals(env);
        for (int q = 0; q < num_shuttles; ++q)
            for (int i = 0; i < max_tasks; ++i)
                for (const auto& label : {"OUT", "RET", "CRGp", "CRGf", "END"})
                    all_intervals.add(task_vars[q][i][label]);

        IloBoolVarArray assign_vars(env);
        for (int q = 0; q < num_shuttles; ++q)
            for (int i = 0; i < max_tasks; ++i)
                for (size_t d = 0; d < demands.size(); ++d)
                    assign_vars.add(a_qid[q][i][d]);

        IloSearchPhase assignment_phase = IloSearchPhase(env, assign_vars);
        IloSearchPhase scheduling_phase = IloSearchPhase(env, all_intervals);

        IloCP cp(model);

        IloSearchPhaseArray phases(env);
        phases.add(scheduling_phase);
        //phases.add(assignment_phase);
        
        cp.setSearchPhases(phases);

        cp.setParameter(IloCP::TimeLimit, config["solver"]["time_limit"].as<int>());
        if (cp.solve()) {
            std::cout << "Solution found. Objective value: " << cp.getObjValue() << std::endl;
            // Proceed to extract and print/write variable values
        } else {
            std::cout << "No solution found." << std::endl;
        }

    std::cout << "battery_capacity: " << battery_capacity << std::endl;
    std::cout << "trip_distance: " << trip_distance << std::endl;
    std::cout << "max_recharge: " << max_recharge << std::endl;
    std::cout << "min_recharge: " << min_recharge << std::endl;
    std::cout << "num_shuttles: " << num_shuttles << std::endl;
    std::cout << "seat_capacity: " << seat_capacity << std::endl;
    std::cout << "max_tasks: " << max_tasks << std::endl;
    std::cout << "trip_duration: " << trip_duration << std::endl;

    for (int i = 0; i < num_shuttles; ++i) {
        for (int j = 0; j < max_tasks; ++j) {
            for (const auto& label : {"OUT", "RET", "END"}) {
                const IloIntervalVar& var = task_vars[i][j][label];
                if (cp.isPresent(var)) {
                    std::cout << "Shuttle " << i
                            << ", Task " << j
                            << ", " << label
                            << ": Start = " << cp.getStart(var)
                            << ", End = " << cp.getEnd(var)
                            << ", Duration = " << cp.getLength(var)
                            << std::endl;
                }
            }
            for (const auto& label : {"CRGp", "CRGf"}) {
                const IloIntervalVar& var = task_vars[i][j][label];
                if (cp.isPresent(var)) {
                    std::cout << "Shuttle " << i
                            << ", Task " << j
                            << ", " << label
                            << ", Start = " << cp.getStart(var)
                            << ", End = " << cp.getEnd(var)
                            << ", Duration = " << cp.getLength(var)
                            // Show recharge rates for CRGp and CRGf
                            << ", Recharge Rate = " << (std::string(label) == "CRGp" ? battery_capacity / 2 : battery_capacity / 6)
                            << std::endl;
                }
            }
        }
    }

    std::ofstream solfile(argv[3]);
    solfile << "{\n";

    solfile << "  \"shuttles\": [\n";
    for (int i = 0; i < num_shuttles; ++i) {
        solfile << "    {\n      \"id\": " << i << ",\n      \"tasks\": [\n";
        for (int j = 0; j < max_tasks; ++j) {
            for (const auto& label : {"OUT", "RET", "END", "CRGp", "CRGf"}) {
                const IloIntervalVar& var = task_vars[i][j][label];
                if (cp.isPresent(var)) {
                    solfile << "        { \"task\": \"" << label << "\", "
                            << "\"start\": " << cp.getStart(var) << ", "
                            << "\"end\": " << cp.getEnd(var) << ", "
                            << "\"duration\": " << cp.getLength(var);
                    // Add recharge rate if CRG
                    if (std::string(label) == "CRGp")
                        solfile << ", \"recharge_rate\": " << (battery_capacity / 2);
                    if (std::string(label) == "CRGf")
                        solfile << ", \"recharge_rate\": " << (battery_capacity / 6);
                    solfile << " },\n";
                }
            }
        }
        solfile << "      ]\n    }";
        if (i < num_shuttles-1) solfile << ",";
        solfile << "\n";
    }
    solfile << "  ],\n";

    solfile << "  \"assignments\": [\n";
    for (size_t d = 0; d < demands.size(); ++d) {
        for (int q = 0; q < num_shuttles; ++q) {
            for (int i = 0; i < max_tasks; ++i) {
                if (cp.getValue(a_qid[q][i][d]) > 0.5) {
                    std::string label = (demands[d].direction == "OUTBOUND") ? "OUT" : "RET";
                    int departure_time = cp.getStart(task_vars[q][i][label]);
                    solfile << "    { \"demand_id\": " << demands[d].req_id
                            << ", \"shuttle\": " << q
                            << ", \"task\": " << i
                            << ", \"direction\": \"" << demands[d].direction << "\""
                            << ", \"req_time\": " << demands[d].time
                            << ", \"departure_time\": " << departure_time
                            << " },\n";
                }
            }
        }
    }

    solfile << "  ]\n";
    solfile << "}\n";

    solfile << "\"parameters\": {\n";
    solfile << "  \"battery_capacity\": " << battery_capacity << ",\n";
    solfile << "  \"trip_distance\": " << trip_distance << ",\n";
    solfile << "  \"max_recharge\": " << max_recharge << ",\n";
    solfile << "  \"min_recharge\": " << min_recharge << ",\n";
    solfile << "  \"num_shuttles\": " << num_shuttles << ",\n";
    solfile << "  \"seat_capacity\": " << seat_capacity << ",\n";
    solfile << "  \"max_tasks\": " << max_tasks << ",\n";
    solfile << "  \"trip_duration\": " << trip_duration << "\n";
    solfile << "},\n";

    solfile.close();


    } catch (IloException& e) {
        std::cerr << "Concert exception: " << e << std::endl;
        exit_code = 1;
    } catch (...) {
        std::cerr << "Unknown exception caught." << std::endl;
        exit_code = 1;
    }
    env.end();
    return exit_code;
}