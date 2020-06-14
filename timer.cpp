#include "timer.h"
#include <algorithm>
#include <iostream>
#include <utility>

using namespace timer_service;
constexpr unsigned long defaultTimedWaitMS = 100;
TimePoint::TimePoint(decltype(m_func) &&function, unsigned long &&ms, bool &&repeat)
	: m_ms(ms), m_timeout(std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(ms)), m_func(function),
	  m_repeat(repeat), m_tid(0)
{
	m_tid = getId();
}

TimePoint::TimePoint(const TimePoint &t)
{
	if (&t != this) {
		m_ms = t.m_ms;
		m_timeout = t.timeout();
		m_tid = t.timerID();
		m_func = t.task();
		m_repeat = t.repeat();
	}
}

unsigned long TimePoint::getId()
{
	static decltype(m_tid) id = 0;
	return ++id;
}

timer::timer() : done(false), notifyNewTimeout(0), taskCleanupStarted(0), deleteTid(0)
{
	timerRunner = std::thread([&]() {
		HighTimePoint till = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(defaultTimedWaitMS);
		do {
			std::unique_lock<std::mutex> lock(taskProtector);
			if (m_timerMap.empty()) {
				till = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(defaultTimedWaitMS);
			} else {
				till = m_timerMap.front().first;
			}

			if (till >= std::chrono::high_resolution_clock::now()) {
				taskCondition.wait_until(lock, till, [this]() -> bool { return (done || notifyNewTimeout); });
			}

			if (notifyNewTimeout) {
				/* resetting the variable for the next notification */
				notifyNewTimeout = false;
			}

			if (done) {
				break;
			}
			if (!m_timerMap.empty()) {
				auto pair = m_timerMap.front();
				if (pair.second.get()->timeout() < std::chrono::high_resolution_clock::now()) {
					m_timerMap.pop_front();
					if (pair.second.get()->timerID() != deleteTid) {
						if (pair.second.get()->repeat()) {
							pair.second.get()->next();
							pair.first = pair.second.get()->timeout();
							m_timerMap.push_back(pair);
							std::sort(m_timerMap.begin(), m_timerMap.end(),
								  [](const TimerPair &_1, const TimerPair &_2) -> bool {
									  return *_1.second < *_2.second;
								  });
						}
						auto task = pair.second.get()->task();
						std::unique_lock<std::mutex> lock(ListProtector);
						futureList.push_back(std::async(std::launch::async, task));
						if (!taskCleanupStarted) {
							/* Clean  up thread will keep clearing the task completed in the futureList*/
							taskCleanupStarted = true;
							cleanupThread = std::thread([this]() { this->ListCleanup(); });
						}
					}
				}
			}
		} while (!done);
	});
}

timer::~timer() { shutDown(); }

void timer::stopTimer(unsigned long tid)
{
	std::lock_guard<std::mutex> lock(taskProtector);
	const auto &position = std::find_if(m_timerMap.begin(), m_timerMap.end(),
					    [tid](const TimerPair &pair) -> bool { return pair.second.get()->timerID() == tid; });
	if (position != m_timerMap.end()) {
		m_timerMap.erase(position);
	}
	deleteTid = tid;
}

bool timer::is_running(unsigned long tid)
{
	std::lock_guard<std::mutex> lock(taskProtector);
	const auto &position = std::find_if(m_timerMap.begin(), m_timerMap.end(),
					    [tid](const TimerPair &pair) -> bool { return pair.second.get()->timerID() == tid; });
	return (position != m_timerMap.end());
}

void timer::shutDown()
{
	done = true;
	taskCleanupStarted = false;
	taskCondition.notify_all();
	if (timerRunner.joinable()) {
		timerRunner.join();
	}
}

void timer::ListCleanup()
{
	do {
		{
			std::unique_lock<std::mutex> lock(ListProtector);
			for (auto it = futureList.begin(); it != futureList.end();) {
				std::future_status status;
				status = it->wait_for(std::chrono::milliseconds(1));
				if (status == std::future_status::ready) {
					it = futureList.erase(it);
				} else {
					it++;
				}
			}
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	} while (taskCleanupStarted);
}

void timer::join()
{
	timerRunner.join();
}

namespace timer_service
{
timer timer_;
}
