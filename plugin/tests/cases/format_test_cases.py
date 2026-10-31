import json
from pathlib import Path


def main() -> None:
    path_cases = Path(__file__).resolve().parent
    for path_case in sorted(path_cases.rglob("*.json")):
        data = json.loads(path_case.read_text(encoding="utf-8"))
        path_case.write_text(
            json.dumps(data, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
