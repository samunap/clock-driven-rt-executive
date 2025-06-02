#include <cassert>
#include <iostream>

#define VERBOSE

#include "executive.h"

Executive::Executive(size_t num_tasks, unsigned int frame_length, unsigned int unit_duration)
	: p_tasks(num_tasks), frame_length(frame_length), unit_time(unit_duration)
{
}

void Executive::set_periodic_task(size_t task_id, std::function<void()> periodic_task, unsigned int /* wcet */)
{
	assert(task_id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	p_tasks[task_id].function = periodic_task;
}
		
void Executive::add_frame(std::vector<size_t> frame)
{
	for (auto & id: frame)
		assert(id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	frames.push_back(frame);
}

void Executive::start()
{
	// Configurazione priorità RT
	rt::priority pri_max(rt::priority::rt_max);

	for (size_t id = 0; id < p_tasks.size(); ++id)
	{
		assert(p_tasks[id].function); // Fallisce se set_periodic_task() non e' stato invocato per questo id
		
		p_tasks[id].thread = std::thread(&Executive::task_function, std::ref(p_tasks[id]));
		
		/* ... */
	}
	
	exec_thread = std::thread(&Executive::exec_function, this);
	
	/* ... */
}
	
void Executive::wait()
{
	exec_thread.join();
	
	for (auto & pt: p_tasks)
		pt.thread.join();
}

void Executive::task_function(Executive::task_data & task)
{
	while (true)
	{
		std::unique_lock<std::mutex> lock(task.mutex);
		task.cv.wait(lock, [&task] {
			return task.released;
		});
		task.function();

		task.completed = true;
		task.released = false;
	}
}

void Executive::exec_function()
{
	int frame_id = 0;
	auto next_frame_start = std::chrono::steady_clock::now();
	

	if (frame.empty()){
		std::cerr << "Error: No frames defined." << std::endl;
		return;
	}

	// Stampa informazioni di configurazione RT
	try{
		rt::priority exec_pri = rt::this_thread::get_priority();
		rt::affinity exec_aff = rt::this_thread::get_affinity();
		std::cout << "Executive started with priority: " << exec_pri << ", affinity: " << exec_aff << std::endl;
	} catch (const std::exception& e){
		std::cerr << "Warning: Could not get executive thread info: " << e.what() << std::endl;
	}


	while (true)
	{
#ifdef VERBOSE
		std::cout << "*** Frame n." << frame_id << (frame_id == 0 ? " ******" : "") << std::endl;
#endif

		next_frame_start += unit_time * frame_length;

		int wcet_sum = 0;
		bool isFinished;

		/* Rilascio dei task periodici del frame corrente ... */
		for (size_t id : frames[frame_id])
		{
			std::unique_lock<std::mutex> lock(p_tasks[id].mutex);
			p_tasks[id].released = true;
			p_tasks[id].completed = false;
			wcet_sum += p_tasks[id].wcet;
			p_tasks[id].cv.notify_one();
		}

		/* Calcolo slack disponibile nel frame corrente */
		int total_frame_time = std::chrono::duration_cast<std::chrono::milliseconds>(unit_time * frame_length).count();
		int slack_time = total_frame_time - wcet_sum;

		/* Attendi fino all'inizio del frame successivo*/
		std::this_thread::sleep_until(next_frame_start);

		/* Controllo delle deadline ... */
		for (size_t id : frames[frame_id])
		{
			std::unique_lock<std::mutex> lock(p_tasks[id].mutex);
			if (!p_tasks[id].completed)
			{
				std::cerr << "Deadlind miss: task " << id << " in frame " << frame_id << std::endl;

				// riduco priorità dopo deadline miss
				rt::priority thread_priority = rt::get_priority(p_tasks[id].thread);
				if (thread_priority > rt::priority::rt_min)
				{
					rt::set_priority(p_tasks[id].thread, rt::priority::rt_min);
					std::cout << "Priorità del task " << id << " diminuita a: " << rt::get_priority(p_tasks[id].thread) << std::endl;
				}
			}
		}
		// avanza frame
		frame_id = (frame_id + 1) % frames.size();
	}
}


