#include <cassert>
#include <iostream>
#include <iomanip>
#include <ctime>

#define VERBOSE

#include "executive.h"

/***************************************************
 * Progetto di Sistemi Operativi in Tempo Reale
 * Autore: Samuele Roberto Napolitano
 * Matricola: 378454
 * Anno Accademico: 2024/2025
 ***************************************************/

Executive::Executive(size_t num_tasks, unsigned int frame_length, unsigned int unit_duration)
	: p_tasks(num_tasks), frame_length(frame_length), unit_time(unit_duration)
{
}

void Executive::set_periodic_task(size_t task_id, std::function<void()> periodic_task, unsigned int wcet)
{
	assert(task_id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	p_tasks[task_id].function = periodic_task;
	p_tasks[task_id].wcet = wcet;
}
		
void Executive::add_frame(std::vector<size_t> frame)
{
	for (auto & id: frame)
		assert(id < p_tasks.size()); // Fallisce in caso di task_id non corretto (fuori range)
	
	frames.push_back(frame);
}

void Executive::start()
{
	try{
		rt::this_thread::set_priority(rt::priority::rt_max);
		std::cout << "Executive thread priority set to: " << rt::this_thread::get_priority() << std::endl;
	} catch(const rt::permission_error& e){
		std::cerr << "Warning: Cannot set executive priority (need root privileges): " << e.what() << std::endl;
	}

	// Configurazione affinity per l'executive (CPU 0)
	try{
		rt::affinity exec_affinity;
		exec_affinity.reset();
		exec_affinity.set(0); // Vincola l'executive alla CPU 0
		rt::this_thread::set_affinity(exec_affinity);
		std::cout << "Executive thread affinity set to CPU 0" << std::endl;
	} catch(const std::exception& e) {
		std::cerr << "Warning: Cannot set executive affinity: " << e.what() << std::endl; 
	}

	// Creazione e configurazione task thread
	for (size_t id = 0; id < p_tasks.size(); ++id)
	{
		assert(p_tasks[id].function); // Fallisce se set_periodic_task() non e' stato invocato per questo id
		
		// Creazione del thread task
		p_tasks[id].thread = std::thread(&Executive::task_function, std::ref(p_tasks[id]));
		
		// Configurazione priorità RT per i task (inferiore all'executive)
		try{
			rt::priority task_priority = rt::priority::rt_max - 1;
			rt::set_priority(p_tasks[id].thread, task_priority);
			std::cout << "Task " << id << " priority set to: " << rt::get_priority(p_tasks[id].thread) << std::endl; 
		} catch (const rt::permission_error& e) {
			std::cerr << "Warning: Cannot set task " << id << " priority: " << e.what() << std::endl;
		}

		// Configurazione affinity per i task
		try{
			rt::affinity task_affinity;
			task_affinity.reset();
			task_affinity.set((id+1) % std::thread::hardware_concurrency()); 
			rt::set_affinity(p_tasks[id].thread, task_affinity);
		} catch(const std::exception& e){
			std::cerr << "Warning: Cannot set task " << id << " affinity" << std::endl;
		}
	}
	
	exec_thread = std::thread(&Executive::exec_function, this);
	
	/* Configurazione priorità massima per il thread executive */
	try{
		rt::set_priority(exec_thread, rt::priority::rt_max);
		std::cout << "Executive thread priority configured to: " << rt::get_priority(exec_thread) << std::endl;
 	} catch (const rt::permission_error& e) {
		std::cerr << "Warning: Cannot set executive thread priority: " << e.what() << std::endl;
	}
}
	
void Executive::wait()
{
	exec_thread.join();
	
	for (auto & pt: p_tasks)
		pt.thread.join();
}

void Executive::task_function(Executive::task_data & task)
{
	while (true) // valutare se usare while(!task.stop)
	{
		std::unique_lock<std::mutex> lock(task.mutex);
		// Attende il rilascio del task da parte dell'executive
		task.cv.wait(lock, [&task] {
			return task.released;
		});

		if (task.stop) break; // uscita pulita dal loop

		// Esecuzione del task
		task.function();

		// Segnalazione del completamento
		task.completed = true;
		task.released = false;
	}
}

void Executive::exec_function()
{
	int frame_id = 0;
	auto next_frame_start = std::chrono::steady_clock::now();
	

	if (frames.empty()){
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

	// Calcolo del tempo di frame in ms per controlli successivi
	const auto frame_duration = unit_time * frame_length;

	while (true)
	{
#ifdef VERBOSE
		std::cout << "*** Frame n." << frame_id << (frame_id == 0 ? " ******" : "") << std::endl;
#endif

		// Calcolo preciso dell'inizio del frame successivo
		next_frame_start += frame_duration;

		// Timestamp per il controllo delle deadline
		auto frame_start_time = std::chrono::steady_clock::now();
		auto deadline = frame_start_time + frame_duration;

		int wcet_sum = 0;

		/* Rilascio dei task periodici del frame corrente ... */
		for (size_t id : frames[frame_id])
		{
			std::unique_lock<std::mutex> lock(p_tasks[id].mutex);
			p_tasks[id].released = true;
			p_tasks[id].completed = false;
			wcet_sum += p_tasks[id].wcet;
			p_tasks[id].cv.notify_one();
		
#ifdef VERBOSE
		std::cout << " Released task " << id << " (WCET: " << p_tasks[id].wcet << ")" << std::endl;
#endif	
		}

		/* Calcolo slack disponibile nel frame corrente */
		int total_frame_time = std::chrono::duration_cast<std::chrono::milliseconds>(frame_duration).count();
		int slack_time = total_frame_time - (wcet_sum * std::chrono::duration_cast<std::chrono::milliseconds>(unit_time).count());

#ifdef VERBOSE
		std::cout << " Frame time: " << total_frame_time << "ms, WCET sum: " << (wcet_sum * std::chrono::duration_cast<std::chrono::milliseconds>(unit_time).count())
				<<"ms, Slack: " << slack_time << "ms" << std::endl;
#endif

		/* Controllo intermedio delle deadline (o metà frame)*/
		auto mid_frame_check = frame_start_time + (frame_duration / 2);
		if (std::chrono::steady_clock::now() < mid_frame_check){
			std::this_thread::sleep_until(mid_frame_check);
		}

		/* Controllo intermedio del completamento dei task*/
		bool all_completed = true;
		for (size_t id : frames[frame_id]){
			std::unique_lock<std::mutex> lock(p_tasks[id].mutex);
			if (!p_tasks[id].completed){
				all_completed = false;
#ifdef VERBOSE
			std::cout << "  Task " << id << " still running at mid-frame check" << std::endl;
#endif			
			}
		}

		/* Attendi fino all'inizio del frame successivo*/
		std::this_thread::sleep_until(next_frame_start);

		/* Controllo delle deadline ... */
		auto current_time = std::chrono::steady_clock::now();
		for (size_t id : frames[frame_id])
		{
			std::unique_lock<std::mutex> lock(p_tasks[id].mutex);
			if (!p_tasks[id].completed){
				auto time_t_now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
				std::cerr << "Deadlind miss: task " << id << " in frame " << frame_id 
						<< " at " << std::put_time(std::localtime(&time_t_now), "%H:%M:%S") << std::endl;

				// Riduco priorità dopo deadline miss
				try{
					rt::priority current_priority = rt::get_priority(p_tasks[id].thread);
					if (current_priority > rt::priority::rt_min){
						rt::set_priority(p_tasks[id].thread, rt::priority::rt_min);
						std::cout << "Priority of taks " << id << " reduced to minimum: " << rt::get_priority(p_tasks[id].thread) << std::endl;
					}

				} catch (const std::exception& e) {
					std::cerr << "Error adjusting priority for task " << id << ": " << e.what() << std::endl;
				}
			}
			else{
#ifdef VERBOSE
				std::cout << "  Task " << id << " completed on time" << std::endl;
			}
#endif
		}
		// avanza frame
		frame_id = (frame_id + 1) % frames.size();
	}
}


