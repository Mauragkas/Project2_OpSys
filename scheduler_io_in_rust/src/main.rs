use std::fs::File;
use std::io::{self, BufRead};
use std::process::{Command, Child};
use std::env;

use std::time::Duration;

use nix::sys::signal::{kill, Signal};
use nix::unistd::Pid;
use std::thread;

#[derive(PartialEq)]
enum ProcessState {
    New,
    Running,
    Stopped,
    Exited,
}

struct Process {
    name: String,
    process_handle: Option<Child>, // Store the Child process handle
    state: ProcessState,
    pid: Pid,
}

fn read_processes_from_file(filename: &str) -> io::Result<Vec<Process>> {
    let file = File::open(filename)?;
    let reader = io::BufReader::new(file);
    let mut processes = Vec::new();

    for line in reader.lines() {
        let app = Process {
            name: line?,
            process_handle: None,
            state: ProcessState::New,
            pid: Pid::from_raw(0),
        };
        processes.push(app);
    }
    Ok(processes)
}

fn start_app(process: &mut Process) -> io::Result<()> {
    let child = Command::new(&process.name).spawn();

    match child {
        Ok(child_process) => {
            process.process_handle = Some(child_process);
            process.state = ProcessState::Running;
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

fn schedule_fcfs(processes: &mut [Process]) {
    for process in processes.iter_mut() {
        if let Err(e) = start_app(process) {
            eprintln!("Failed to start process {}: {}", process.name, e);
            continue;
        }

        if let Some(child) = &mut process.process_handle {
            let _ = child.wait(); // Wait for the process to finish
            process.state = ProcessState::Exited;
        }
    }
}

fn update_process_states(processes: &mut [Process]) {
    for process in processes.iter_mut() {
        if let Some(child) = &mut process.process_handle {
            match child.try_wait() {
                Ok(Some(status)) => {
                    if status.success() {
                        process.state = ProcessState::Exited;
                    } else {
                        // Handle the case where the process did not exit successfully
                    }
                }
                Ok(None) => {
                    // The process is still running; no action needed
                }
                Err(_e) => {
                    // Handle the error case
                }
            }
        }
    }
}

fn check_all_exited(processes: &[Process]) -> bool {
    processes.iter().all(|p| p.state == ProcessState::Exited)
}

fn schedule_rr(processes: &mut [Process], quantum: Duration) {
    let mut all_exited = false;

    while !all_exited {
        for process in processes.iter_mut() {
            if process.state == ProcessState::Exited {
                continue;
            }

            if process.state == ProcessState::New {
                if let Err(e) = start_app(process) {
                    eprintln!("Failed to start process {}: {}", process.name, e);
                    continue;
                }
            }

            if process.state == ProcessState::Stopped {
                // Resume the process
                let _ = kill(process.pid, Signal::SIGCONT);
                process.state = ProcessState::Running;
            }

            thread::sleep(quantum);

            // Stop the process
            let _ = kill(process.pid, Signal::SIGSTOP);
            process.state = ProcessState::Stopped;
        }

        update_process_states(processes);

        all_exited = check_all_exited(processes);
    }
}

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 3 {
        eprintln!("Usage: {} <policy> [<quantum>] <input_filename>", args[0]);
        return Ok(());
    }

    let policy = &args[1];
    let mut processes = read_processes_from_file(&args[args.len() - 1])?;

    match policy.as_str() {
        "FCFS" => schedule_fcfs(&mut processes),
        "RR" => {
            let quantum_millis = args[2].parse::<u64>()
                .expect("Invalid quantum: please enter a number of seconds");
            let quantum = Duration::from_millis(quantum_millis);
            schedule_rr(&mut processes, quantum);
        }
        _ => eprintln!("Invalid scheduling policy."),
    }

    Ok(())
}
