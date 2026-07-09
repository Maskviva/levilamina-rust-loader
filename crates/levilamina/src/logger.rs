//! Per-mod logging routed through LeviLamina's logging system. Thread-safe.

use crate::ffi::s;
use crate::rt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogLevel {
    Fatal = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
}

/// Per-mod logger. Cheap to copy; safe from any thread.
#[derive(Clone, Copy)]
pub struct Logger(());

impl Logger {
    pub fn get() -> Logger {
        Logger(())
    }
    pub fn log(&self, level: LogLevel, msg: &str) {
        let rt = rt();
        unsafe { (rt.api.log)(rt.handle, level as i32, s(msg)) }
    }
    pub fn info(&self, msg: &str) {
        self.log(LogLevel::Info, msg);
    }
    pub fn warn(&self, msg: &str) {
        self.log(LogLevel::Warn, msg);
    }
    pub fn error(&self, msg: &str) {
        self.log(LogLevel::Error, msg);
    }
    pub fn debug(&self, msg: &str) {
        self.log(LogLevel::Debug, msg);
    }
    pub fn trace(&self, msg: &str) {
        self.log(LogLevel::Trace, msg);
    }
}
