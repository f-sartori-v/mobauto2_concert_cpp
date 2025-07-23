# visualisation.py

import pandas as pd
import plotly.express as px

import pandas as pd
from datetime import datetime, timedelta


def extract_schedule(solution_dict):
    """
    Extracts a structured schedule DataFrame from the raw CP solution dictionary.

    Args:
        solution_dict (dict): Raw dictionary from `result.get_all_var_solutions()`
        demand_df (DataFrame): Demand data with request info

    Returns:
        schedule_df (pd.DataFrame): Structured Gantt-compatible schedule with passenger assignments
    """
    schedule = []

    # Helper to identify active interval variables
    def is_active_interval(val):
        return hasattr(val, "start") and hasattr(val, "end")

    # Track passenger assignments by (q, i)
    passenger_lookup = {}
    for k, v in solution_dict.items():
        if k.startswith("a_q") and v == 1:
            # e.g., 'a_q0_i5_d7'
            parts = k.split("_")
            q = int(parts[1][1:])
            i = int(parts[2][1:])
            d = int(parts[3][1:])
            passenger_lookup.setdefault((q, i), []).append(d)

    # Track battery start and end if present
    battery_start = {}
    battery_end = {}
    for varname, val in solution_dict.items():
        if varname.startswith("bstart_q"):
            parts = varname.split("_")
            q = int(parts[1][1:])
            i = int(parts[2][1:])
            battery_start[(q, i)] = val
        elif varname.startswith("bend_q"):
            parts = varname.split("_")
            q = int(parts[1][1:])
            i = int(parts[2][1:])
            battery_end[(q, i)] = val

    # Now parse interval tasks
    for varname, val in solution_dict.items():
        if not is_active_interval(val):
            continue

        # e.g., 'q0_i5_RET'
        parts = varname.split("_")
        if len(parts) != 3:
            continue  # skip non-task entries

        q = int(parts[0][1:])  # shuttle index
        i = int(parts[1][1:])  # task index
        task_type = parts[2]  # OUT, RET, REC, IDL, IDM

        passengers = passenger_lookup.get((q, i), [])

        schedule.append({
            "shuttle": q,
            "task_index": i,
            "task_type": task_type,
            "start": val.start,
            "end": val.end,
            "duration": val.size,
            "passengers": passengers,
            "n_passengers": len(passengers),
            "battery_start": battery_start.get((q, i), None),
            "battery_end": battery_end.get((q, i), None)
        })

    schedule_df = pd.DataFrame(schedule)
    return schedule_df


def process_schedule_df(schedule_df, time_res):
    # 1. Convert time units to clock time starting at 07:00
    base_time = datetime.strptime("07:00", "%H:%M")
    schedule_df["start_dt"] = schedule_df["start"].apply(lambda x: base_time + timedelta(minutes=x * time_res))
    schedule_df["end_dt"] = schedule_df["end"].apply(lambda x: base_time + timedelta(minutes=x * time_res))

    # 2. Colour mapping by group
    def classify(task):
        if task == "OUT":
            return "out"
        if task == "RET":
            return "ret"
        elif task in ["IDL", "IDM"]:
            return "idl"
        elif task == "REC":
            return "rec"
        elif task == "END":
            return "end"
        else:
            return "other"

    schedule_df["task_group"] = schedule_df["task_type"].apply(classify)

    return schedule_df


import plotly.express as px

def plot_gantt(schedule_df, time_res):
    df = process_schedule_df(schedule_df, time_res)

    color_map = {
        "out": "orange",
        "ret": "red",
        "idl": "blue",
        "rec": "green",
        "end": "gray"
    }

    fig = px.timeline(
        df,
        x_start="start_dt",
        x_end="end_dt",
        y="shuttle",
        color="task_group",
        color_discrete_map=color_map,
        hover_data=["task_type", "task_index", "passengers", "battery_start", "battery_end"],
    )

    # Flip y-axis so shuttle 0 is at top
    fig.update_yaxes(autorange="reversed")

    # Add number of passengers as annotation on each bar
    for i, row in df.iterrows():
        passengers = row["passengers"]
        if isinstance(passengers, list) and len(passengers) > 0:
            fig.add_annotation(
                x=row["start_dt"] + (row["end_dt"] - row["start_dt"]) / 2,
                y=row["shuttle"],
                text=len(passengers),
                showarrow=False,
                font=dict(color="white", size=12),
                yanchor="middle"
            )

        # Show battery as text annotation
        if pd.notnull(row["battery_start"]) and pd.notnull(row["battery_end"]) and row["task_type"] in ["OUT", "RET", "REC"]:
            fig.add_annotation(
                x=row["start_dt"] + (row["end_dt"] - row["start_dt"]) / 2,
                y=row["shuttle"] + 0.2,
                text=f'{int(row["battery_start"])}→{int(row["battery_end"])}',
                showarrow=False,
                font=dict(color="black", size=7),
                yanchor="middle"
            )

    fig.update_layout(
        title="Shuttle Task Schedule",
        xaxis_title="Time",
        yaxis_title="Shuttle ID"
    )

    fig.show()
