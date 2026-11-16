# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Check the experimental scheduler policy model
# COPYRIGHT:   Copyright 2026 LiberNT contributors

import unittest
from fractions import Fraction

from eevdf_model import Dispatcher, Thread


class SchedulerModelTest(unittest.TestCase):
    def test_priority_is_strict_in_both_policies(self):
        for policy in ('nt', 'eevdf'):
            scheduler = Dispatcher(policy)
            low = Thread('low', 8)
            high = Thread('high', 9)
            scheduler.wake(low)
            scheduler.wake(high)
            for _ in range(100):
                self.assertIs(scheduler.run(), high)
            self.assertEqual(low.runtime, 0)

    def test_realtime_remains_round_robin(self):
        scheduler = Dispatcher('eevdf')
        a, b = Thread('a', 24, slice=2), Thread('b', 24, slice=2)
        scheduler.wake(a)
        scheduler.wake(b)
        self.assertEqual([scheduler.run().name for _ in range(8)],
                         ['a', 'a', 'b', 'b', 'a', 'a', 'b', 'b'])

    def test_weighted_service_within_a_priority(self):
        scheduler = Dispatcher('eevdf')
        a, b = Thread('a', 8, weight=4), Thread('b', 8)
        scheduler.wake(a)
        scheduler.wake(b)
        for _ in range(1000):
            scheduler.run()
        self.assertEqual((a.runtime, b.runtime), (800, 200))

    def test_shorter_slice_has_earlier_deadline(self):
        scheduler = Dispatcher('eevdf')
        bulk = Thread('bulk', 8, slice=8)
        ui = Thread('ui', 8, slice=2)
        scheduler.wake(bulk)
        scheduler.wake(ui)
        self.assertIs(scheduler.choose(), ui)
        for _ in range(100):
            scheduler.run()
        self.assertLessEqual(abs(bulk.runtime - ui.runtime), 2)

    def test_ui_wakeup_competes_with_a_running_peer(self):
        for policy in ('nt', 'eevdf'):
            scheduler = Dispatcher(policy)
            bulk = Thread('bulk', 8, slice=8)
            ui = Thread('ui', 8, slice=2)
            scheduler.wake(bulk)
            scheduler.run()
            scheduler.wake(ui)
            expected = ui if policy == 'eevdf' else bulk
            self.assertIs(scheduler.choose(), expected)

    def test_priority_boost_preempts_and_returns_to_original_level(self):
        scheduler = Dispatcher('eevdf')
        bulk, ui = Thread('bulk', 8), Thread('ui', 8)
        scheduler.wake(bulk)
        scheduler.wake(ui)
        scheduler.run()
        scheduler.set_priority(ui, 10)
        self.assertIs(scheduler.choose(), ui)
        scheduler.set_priority(ui, 8)
        self.assertEqual(ui.priority, 8)
        self.assertEqual(len(scheduler.ready), 2)

    def test_priority_roundtrip_preserves_level_debt(self):
        scheduler = Dispatcher('eevdf')
        a, b = Thread('a', 8), Thread('b', 8)
        scheduler.wake(a)
        scheduler.wake(b)
        scheduler.run()
        scheduler.set_priority(a, 9)
        self.assertLess(a.priority_lags[8], 0)
        scheduler.set_priority(a, 8)
        self.assertIs(scheduler.choose(), b)

    def test_short_sleep_preserves_negative_lag(self):
        scheduler = Dispatcher('eevdf')
        a, b = Thread('a', 8), Thread('b', 8)
        scheduler.wake(a)
        scheduler.wake(b)
        scheduler.run()
        scheduler.sleep(a)
        self.assertLess(a.saved_lag, 0)
        scheduler.wake(a)
        self.assertIs(scheduler.choose(), b)

    def test_repeated_sleep_does_not_erase_debt(self):
        scheduler = Dispatcher('eevdf')
        a, b = Thread('a', 8), Thread('b', 8)
        scheduler.wake(a)
        scheduler.wake(b)
        scheduler.run()
        for _ in range(20):
            scheduler.sleep(a)
            scheduler.wake(a)
            self.assertIs(scheduler.choose(), b)

    def test_lag_is_preserved_when_joining_a_busy_queue(self):
        scheduler = Dispatcher('eevdf')
        a, b = Thread('a', 8), Thread('b', 8)
        scheduler.wake(a)
        scheduler.wake(b)
        scheduler.run()
        scheduler.sleep(a)
        before = a.saved_lag
        scheduler.wake(a)
        level = scheduler._level(8)
        self.assertEqual(scheduler._time(level, 8) - a.vruntime, before)

    def test_idle_and_invalid_inputs(self):
        scheduler = Dispatcher('eevdf')
        self.assertIsNone(scheduler.run())
        with self.assertRaises(ValueError):
            Thread('bad', 0)
        with self.assertRaises(ValueError):
            Dispatcher('bad')
        thread = Thread('a', 8)
        scheduler.wake(thread)
        with self.assertRaises(ValueError):
            scheduler.wake(thread)
        with self.assertRaises(ValueError):
            scheduler.run(Fraction(0))


if __name__ == '__main__':
    unittest.main()
