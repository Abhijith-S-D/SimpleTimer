
#ifndef TIMER_H
#define TIMER_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace timer_service
{

using HighTimePoint = std::chrono::high_resolution_clock::time_point;

class TimePoint final
{
private:
	unsigned long m_ms;
	HighTimePoint m_timeout;
	mutable std::function<void()> m_func;
	bool m_repeat;
	unsigned long m_tid;
	mutable std::mutex m_timePointProtect;

public:
	explicit TimePoint(decltype(m_func) &&function, unsigned long &&ms = 0, bool &&repeat = false);

	TimePoint(const TimePoint &t);

	~TimePoint() = default;

	bool operator>(const TimePoint &point) const { return (m_timeout > point.timeout()); }
	bool operator<(const TimePoint &point) const { return (m_timeout < point.timeout()); }

	decltype(m_timeout) timeout() const
	{
		std::lock_guard<std::mutex> lock(m_timePointProtect);
		return m_timeout;
	}
	decltype(m_func) task() const { return m_func; }
	static unsigned long getId();
	decltype(m_repeat) repeat() const { return m_repeat; }
	decltype(m_tid) timerID() const { return m_tid; }

	void next()
	{
		std::lock_guard<std::mutex> lock(m_timePointProtect);
		m_timeout += std::chrono::milliseconds(m_ms);
	}
};

using TimerPair = std::pair<HighTimePoint, std::shared_ptr<TimePoint>>;
class timer final
{
private:
	mutable std::deque<TimerPair> m_timerMap;
	std::thread timerRunner;
	std::atomic<bool> done;
	std::atomic<bool> notifyNewTimeout;
	std::atomic<bool> taskCleanupStarted;
	std::mutex taskProtector;
	std::mutex ListProtector;
	std::condition_variable taskCondition;
	std::vector<std::future<void>> futureList;
	std::thread cleanupThread;
	std::atomic<unsigned long> deleteTid; // Needed to identify whether the stopped timer is of the one which got timedout.

public:
	timer();
	~timer();

	template <class callable, class... Args> unsigned long schedule(unsigned long &&ms, bool &&repeat, callable &&f, Args &&... args)
	{
		std::lock_guard<std::mutex> lock(taskProtector);
		auto func(std::bind(std::forward<callable>(f), std::forward<decltype(args)>(args)...));
		auto ptr = std::make_shared<TimePoint>(TimePoint(func, std::forward<unsigned long>(ms), std::forward<bool>(repeat)));
		auto timerID = ptr.get()->timerID();
		m_timerMap.push_back(
		    std::make_pair<HighTimePoint, std::shared_ptr<TimePoint>>(ptr.get()->timeout(), std::forward<decltype(ptr)>(ptr)));
		std::sort(m_timerMap.begin(), m_timerMap.end(),
			  [](TimerPair &_1, TimerPair &_2) -> bool { return _1.second.get()->timeout() < _2.second.get()->timeout(); });
		auto pair = m_timerMap.front();
		/* To check if the new timer that is being added may timeout earlier than the first timer in the queue*/
		if (pair.second.get()->timerID() == timerID && m_timerMap.size() != 1) {
			/* timer thread might be blocking in the conditional variable for the timeout to notify it*/
			notifyNewTimeout = true;
		} else {
			// logd("The timerId is %d and map size is %d", pair.second.get()->timerID(), m_timerMap.size());
		}
		taskCondition.notify_all();
		return timerID;
	}

	void stopTimer(unsigned long tid);
	void ListCleanup();
	void shutDown();
	bool is_running(unsigned long tid);
};
} // namespace timer_service
#endif // TIMER_H
