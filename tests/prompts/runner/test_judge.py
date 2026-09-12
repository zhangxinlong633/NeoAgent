# test_judge.py — mock HTTP for judge JSON parse
import json
import unittest
from unittest.mock import MagicMock, patch

import judge


class TestJudge(unittest.TestCase):
    @patch.dict(
        "os.environ",
        {
            "NEO_JUDGE_BASE_URL": "https://example.invalid/v1",
            "NEO_JUDGE_API_KEY": "x",
            "NEO_JUDGE_MODEL": "m",
        },
    )
    @patch("judge.urllib.request.urlopen")
    def test_parse_pass(self, urlopen):
        payload = {
            "choices": [
                {"message": {"content": json.dumps({"status": "pass", "reason": "ok"})}}
            ]
        }
        cm = MagicMock()
        cm.read.return_value = json.dumps(payload).encode()
        cm.__enter__.return_value = cm
        cm.__exit__.return_value = False
        urlopen.return_value = cm
        r = judge.judge_case(
            {"expect": {"outcome": "refuse"}, "prompt": "x"},
            "无法诊断",
            "",
            {"status": "gray"},
        )
        self.assertEqual(r["status"], "pass")


if __name__ == "__main__":
    unittest.main()
