"""Host checks for the fixture-preparation gate; no Unreal execution."""
import unittest
from room_battle_preparation import prepare_battle


class PreparationTests(unittest.TestCase):
    def exercise(self, *, travel=False, intermediate=None, native_frame=0, public_active=False):
        events = {
            "server": [{"completed_map_loads": 2, "world_map": "/Old"}],
            "client0": [{"completed_map_loads": 2, "world_map": "/Old",
                         "owner_channel_ready": True, "current_membership": "current-generation"}],
        }
        calls = []
        prepared = {"completed_map_loads": 3 if travel else 2, "world_map": "/Selected",
                    "prepared_battle_serial": 7, "prepared_battle_ready": True,
                    "battle_frame": native_frame, "configuration_seed": 9009,
                    "active_match": public_active, "preparation_travel": travel}
        case = self

        class Group:
            def events(self, role): return events[role]
            def command(self, role, op, **fields):
                calls.append((role, op))
                if op == "prepare-battle":
                    case.assertEqual(fields, {"fighters": ["host", "peer"]})
                    return {"prepared_battle_serial": 7, "configuration_seed": 9009}
                case.assertEqual(op, "snapshot")
                case.assertEqual(fields, {})
                return {"active_match": public_active}
            def wait_for(self, predicate, label, timeout):
                case.assertEqual(timeout, 60)
                if label.startswith("configured"):
                    if intermediate:
                        events["server"].append(dict(prepared, **intermediate))
                        case.assertFalse(predicate())
                    events["server"].append(prepared)
                    case.assertTrue(predicate())
                else:
                    case.assertFalse(predicate())
                    events["client0"].append({"completed_map_loads": 2, "world_map": "/Selected",
                                               "owner_channel_ready": True, "current_membership": "current-generation"})
                    case.assertEqual(predicate(), not travel)
                    events["client0"].append({"completed_map_loads": 3, "world_map": "/Selected",
                                               "owner_channel_ready": True, "current_membership": "current-generation"})
                    case.assertTrue(predicate())

        result = prepare_battle(Group(), ("client0",), fighters=("host", "peer"))
        self.assertEqual(calls, [("server", "prepare-battle"), ("server", "snapshot")])
        self.assertFalse(result["active_match"], "fixture preparation does not establish public start")

    def test_preparation_sends_no_start_or_input(self):
        self.exercise()

    def test_travel_requires_actual_participant_map_completion(self):
        self.exercise(travel=True)

    def test_previous_preparation_cannot_satisfy_new_request(self):
        self.exercise(intermediate={"prepared_battle_serial": 6})

    def test_wrong_seed_cannot_satisfy_preparation(self):
        self.exercise(intermediate={"configuration_seed": 12})

    def test_nonzero_prestart_native_frame_does_not_replace_public_start(self):
        self.exercise(native_frame=17)

    def test_premature_public_activation_fails_preparation(self):
        with self.assertRaisesRegex(AssertionError, "preparation must leave public match inactive"):
            self.exercise(public_active=True)


if __name__ == "__main__":
    unittest.main()
