import numpy as np


class ScopeHistory:
    def __init__(self, channels, capacity=96000, rate=48000):
        self.capacity = capacity
        self.rate = rate
        self.data = np.zeros((channels, capacity), dtype=np.float32)
        self.times = np.full(capacity, -np.inf)
        self.total = 0

    def write(self, samples, dac_time):
        count = samples.shape[1]
        offset = max(0, count - self.capacity)
        indexes = (self.total + np.arange(offset, count)) % self.capacity
        self.data[:, indexes] = samples[:, offset:]
        self.times[indexes] = dac_time + np.arange(offset, count) / self.rate
        self.total += count

    def read(self, now, count=4800):
        size = min(self.total, self.capacity)
        indexes = (self.total - size + np.arange(size)) % self.capacity
        times = self.times[indexes]
        end = int(np.searchsorted(times, now, side='right'))
        start = max(0, end - count)
        selected = indexes[start:end]
        result = self.data[:, selected].copy()
        if selected.size > 1:
            gaps = np.flatnonzero(np.diff(self.times[selected]) > 1.5 / self.rate)
            if gaps.size:
                result = result[:, int(gaps[-1]) + 1:]
        return result
