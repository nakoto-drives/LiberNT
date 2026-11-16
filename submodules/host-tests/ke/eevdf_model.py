# PROJECT:     LiberNT host-native tests
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Executable model of the experimental scheduler policy
# COPYRIGHT:   Copyright 2026 LiberNT contributors

from dataclasses import dataclass, field
from fractions import Fraction


@dataclass(eq=False)
class Thread:
    name: str
    priority: int
    weight: int = 1
    slice: int = 4
    runtime: Fraction = Fraction(0)
    vruntime: Fraction = Fraction(0)
    deadline: Fraction = Fraction(0)
    remaining: Fraction = Fraction(0)
    saved_lag: Fraction = Fraction(0)
    priority_lags: dict = field(default_factory=dict)
    initialized: bool = False
    runnable: bool = False

    def __post_init__(self):
        if not 1 <= self.priority <= 31 or self.weight <= 0 or self.slice <= 0:
            raise ValueError('invalid priority, weight or slice')


class Dispatcher:
    def __init__(self, policy='nt'):
        if policy not in ('nt', 'eevdf'):
            raise ValueError('unknown scheduler policy')
        self.policy = policy
        self.ready = []
        self.current = None
        self.virtual_time = {}

    def _level(self, priority):
        return [thread for thread in self.ready if thread.priority == priority]

    def _time(self, level, priority):
        if not level:
            return self.virtual_time.get(priority, Fraction(0))
        return sum((thread.vruntime * thread.weight for thread in level),
                   Fraction(0)) / sum(thread.weight for thread in level)

    def _request(self, thread):
        thread.remaining = Fraction(thread.slice)
        thread.deadline = thread.vruntime + Fraction(thread.slice, thread.weight)

    def wake(self, thread):
        if thread.runnable:
            raise ValueError('thread is already runnable')
        level = self._level(thread.priority)
        virtual_time = self._time(level, thread.priority)
        if level:
            weight = sum(item.weight for item in level)
            lag = thread.saved_lag if thread.initialized else Fraction(0)
            bound = Fraction(thread.slice, thread.weight)
            lag = max(-bound, min(bound, lag))
            thread.vruntime = virtual_time - lag * (weight + thread.weight) / weight
        else:
            thread.vruntime = virtual_time
        thread.runnable = True
        thread.initialized = True
        self._request(thread)
        self.ready.append(thread)

    def sleep(self, thread):
        if not thread.runnable:
            raise ValueError('thread is not runnable')
        level = self._level(thread.priority)
        virtual_time = self._time(level, thread.priority)
        thread.saved_lag = virtual_time - thread.vruntime
        thread.priority_lags[thread.priority] = thread.saved_lag
        self.virtual_time[thread.priority] = virtual_time
        self.ready.remove(thread)
        thread.runnable = False
        if self.current is thread:
            self.current = None

    def set_priority(self, thread, priority):
        if not 1 <= priority <= 31:
            raise ValueError('invalid priority')
        if priority == thread.priority:
            return
        runnable = thread.runnable
        if runnable:
            self.sleep(thread)
        thread.priority = priority
        thread.saved_lag = thread.priority_lags.get(priority, Fraction(0))
        if runnable:
            self.wake(thread)

    def choose(self):
        if not self.ready:
            self.current = None
            return None
        priority = max(thread.priority for thread in self.ready)
        level = self._level(priority)
        if self.policy == 'nt' or priority >= 16:
            if self.current in level and self.current.remaining > 0:
                return self.current
            self.current = level[0]
            return self.current
        virtual_time = self._time(level, priority)
        eligible = [thread for thread in level if thread.vruntime <= virtual_time]
        self.current = min(eligible, key=lambda thread: thread.deadline)
        return self.current

    def run(self, duration=1):
        duration = Fraction(duration)
        if duration <= 0:
            raise ValueError('duration must be positive')
        thread = self.choose()
        if thread is None:
            return None
        if duration > thread.remaining:
            raise ValueError('duration exceeds remaining slice')
        thread.runtime += duration
        thread.vruntime += duration / thread.weight
        thread.remaining -= duration
        if thread.remaining == 0:
            self._request(thread)
            self.ready.remove(thread)
            self.ready.append(thread)
            self.current = None
        return thread
