import json
import sys
from typing import Dict, Any

def generate(data: Dict[str, Any]) -> str:
    output = []
    for pass_name, pass_data in data["passes"].items():
        if pass_name == None:
            continue

        for x in data["passes"][pass_name]["inputs"]:
            if any(x): x


        output = output + [f"""std::vector<FrameGraph::Input> {pass_name}_inputs = {{"""]
        output = output + [f"""}}"""]


    return output
def main():
    with open(sys.argv[1], "r") as f:
        generate(json.loads(f.read()))

main()