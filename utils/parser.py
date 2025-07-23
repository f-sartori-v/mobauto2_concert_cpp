import pandas as pd
import random

def load_demand(filepath: str, time_res) -> pd.DataFrame:
    """
    Load demand profile from CSV.
    Expected format:
        slot, direction, passengers
        1, O, 5
        31, R, 4
        ...
    Returns a DataFrame indexed by [req_id, req_time, direction].
    """
    df = pd.read_csv(filepath)

    required_cols = {"slot", "passengers", "direction"}
    if not required_cols.issubset(df.columns):
        raise ValueError(f"Demand file must include columns: {required_cols}")

    passengers = []
    req_id = 0
    for _, row in df.iterrows():
        start_min = int(row["slot"]) * 30 / time_res
        direction = row["direction"].strip().upper()

        for dem in range(int(row["passengers"])):
            #start_min = int(row["slot"]) * 30 / time_res  + random.randint(0, int((30/time_res)) - 1) * time_res
            passengers.append({'req_id': req_id, 'direction': direction, 'time': start_min})
            req_id += 1

    return pd.DataFrame(passengers).sort_values("time").reset_index(drop=True)
