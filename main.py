import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

import yaml
import subprocess

from utils.parser import load_demand
from utils.visualization import extract_schedule, plot_gantt

def main():
    # Load configuration
    with open("data/config.yaml", "r") as f:
        config = yaml.safe_load(f)

    # Load data
    demand_df = load_demand(config["data"]["demand_file"], config["time_res"])
    demand_df.to_csv("data/demand.csv", index=False)

    # Call the C++ solver
    subprocess.run([
        "./cpp/solver",
        "data/config.yaml",
        "data/demand.csv"
        "solution.json"
    ], check=True)

    #schedule_df = extract_schedule("solution.json")

if __name__ == "__main__":
    main()