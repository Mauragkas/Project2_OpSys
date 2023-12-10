use std::collections::VecDeque;
use std::fs::File;
use std::io::{self, BufRead};
use std::process::{Command, Child};
use std::env;

use std::time::Duration;

use nix::sys::signal::{kill, Signal};
use nix::unistd::Pid;
use std::thread;

use std::sync::{Arc, Mutex};

use std::sync::atomic::{AtomicBool, Ordering};
use nix::sys::wait::{waitpid, WaitStatus}; // Add this import

#[derive(PartialEq)]
enum ProcessState {
    NEW,
    RUNNING,
    STOPPED,
    EXITED,
}

struct Process {
    name: String,
    process_handle: Option<Child>, // Store the Child process handle
    state: ProcessState,
    pid: Pid,
}

static mut PROCESSES: Option<Arc<Mutex<Vec<Process>>>> = None;
static SCHEDULING_DONE: AtomicBool = AtomicBool::new(false);

fn read_processes_from_file(filename: &str) {
    let file = File::open(filename).unwrap();
    let mut processes = Vec::new();

    for line in io::BufReader::new(file).lines() {
        let line = line.unwrap();
        let mut split = line.split_whitespace();
        let name = split.next().unwrap().to_string();
        let state = ProcessState::NEW;
        let process = Process {
            name,
            process_handle: None,
            state,
            pid: Pid::from_raw(0),
        };
        processes.push(process);
    }

    unsafe {
        PROCESSES = Some(Arc::new(Mutex::new(processes)));
    }
}

fn start_process(process: &mut Process) -> io::Result<()> {
    let child = Command::new(&process.name).spawn();

    match child {
        Ok(child_process) => {
            process.process_handle = Some(child_process);
            process.state = ProcessState::RUNNING;
            process.pid = Pid::from_raw(process.process_handle.as_ref().unwrap().id() as i32);
            // println!("Started process {} with pid {}", process.name, process.pid);
            Ok(())
        }
        Err(e) => {
            eprintln!("Failed to start process: {}", e);
            Err(e)
        }
    }
}

fn schedule_fcfs() {
    let mut processes = unsafe { PROCESSES.as_ref().unwrap().lock().unwrap() };
    for process in processes.iter_mut() {
        if let Err(e) = start_process(process) {
            eprintln!("Error starting process {}: {}", process.name, e);
            continue;
        }
        if let Some(child) = &mut process.process_handle {
            match child.wait() {
                Ok(status) => {
                    // println!("Process {} exited with status {}", process.name, status);
                    process.state = ProcessState::EXITED;
                }
                Err(e) => eprintln!("Failed to wait on process {}: {}", process.name, e),
            }
        }
    }
}

fn signal_handler(processes: Arc<Mutex<Vec<Process>>>) {
    println!("Signal handler thread started");
    while !SCHEDULING_DONE.load(Ordering::Relaxed) {
        let mut processes = processes.lock().unwrap();
        for process in processes.iter_mut() {
            if let Some(child) = &mut process.process_handle {
                match waitpid(Pid::from_raw(child.id() as i32), None) {
                    Ok(WaitStatus::Exited(_, _)) | Ok(WaitStatus::Signaled(_, _, _)) => {
                        println!("Process {} exited", process.name);
                        process.state = ProcessState::EXITED;
                    }
                    _ => (),
                }
            }
        }
        thread::sleep(Duration::from_millis(100)); // Polling interval
    }
    println!("Signal handler thread finished");
}

fn schedule_rr(quantum: Duration) {
    let mut processes = unsafe { PROCESSES.as_ref().unwrap().lock().unwrap() };
    let mut queue: VecDeque<&mut Process> = processes.iter_mut().collect();

    while !queue.is_empty() {
        let mut all_exited = true;

        for process in queue.iter_mut() {
            match process.state {
                ProcessState::NEW => {
                    start_process(process).unwrap();
                }
                ProcessState::RUNNING => {
                    thread::sleep(quantum);
                    kill(Pid::from_raw(process.pid.as_raw()), Signal::SIGSTOP).unwrap();
                    process.state = ProcessState::STOPPED;
                }
                ProcessState::STOPPED => {
                    kill(Pid::from_raw(process.pid.as_raw()), Signal::SIGCONT).unwrap();
                    process.state = ProcessState::RUNNING;
                }
                _ => (),
            }

            if process.state != ProcessState::EXITED {
                all_exited = false;
            }
        }

        if all_exited {
            break;
        }

       thread::sleep(Duration::from_millis(100)); // Small delay to avoid busy waiting
    }
}

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: {} <policy> [<quantum>] <input_filename>", args[0]);
        return Ok(());
    }

    read_processes_from_file(&args[args.len() - 1]);

    let processes = unsafe { PROCESSES.clone().unwrap() }; // Updated to unwrap correctly
    let handler_thread = thread::spawn(move || {
        signal_handler(processes);
    });

    let policy = &args[1];
    match policy.as_str() {
        "FCFS" => schedule_fcfs(),
        "RR" => {
            if args.len() < 4 {
                eprintln!("Usage: {} RR <quantum> <input_filename>", args[0]);
                return Ok(());
            }
            let quantum_millis = args[2].parse::<u64>()
                .expect("Invalid quantum: please enter a number of milliseconds");
            let quantum = Duration::from_millis(quantum_millis);
            schedule_rr(quantum);
        }
        _ => eprintln!("Invalid scheduling policy."),
    }


    SCHEDULING_DONE.store(true, Ordering::Relaxed);

    handler_thread.join().unwrap(); // Ensure the signal handler thread finishes

    Ok(())
}