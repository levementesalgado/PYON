/// Store NoSQL — NDJSON com flock para acesso concorrente seguro.

use std::fs::{self, File, OpenOptions};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};
use serde::{de::DeserializeOwned, Serialize};
use serde_json::Value;

#[cfg(unix)]
use std::os::unix::io::AsRawFd;

pub struct Store {
    base: PathBuf,
}

// flock LOCK_EX / LOCK_UN via libc syscall direto — sem dep extra
#[cfg(unix)]
fn lock_ex(f: &File) {
    unsafe { libc_flock(f.as_raw_fd(), 2); } // LOCK_EX = 2
}
#[cfg(unix)]
fn unlock(f: &File) {
    unsafe { libc_flock(f.as_raw_fd(), 8); } // LOCK_UN = 8
}
#[cfg(unix)]
extern "C" { fn flock(fd: i32, op: i32) -> i32; }
#[cfg(unix)]
unsafe fn libc_flock(fd: i32, op: i32) { flock(fd, op); }

#[cfg(not(unix))]
fn lock_ex(_f: &File) {}
#[cfg(not(unix))]
fn unlock(_f: &File) {}

impl Store {
    pub fn open(base: impl AsRef<Path>) -> std::io::Result<Self> {
        fs::create_dir_all(base.as_ref())?;
        Ok(Self { base: base.as_ref().to_path_buf() })
    }

    fn table_path(&self, table: &str) -> PathBuf {
        self.base.join(format!("{}.ndjson", table))
    }

    /// Insere um registro (append). Usa flock para evitar race condition.
    pub fn insert<T: Serialize>(&self, table: &str, record: &T) -> std::io::Result<()> {
        let mut f = OpenOptions::new()
            .create(true).append(true)
            .open(self.table_path(table))?;
        lock_ex(&f);
        let line = serde_json::to_string(record).unwrap();
        let r = writeln!(f, "{}", line);
        unlock(&f);
        r
    }

    /// Lê todos os registros. Sem lock (leitura eventual consistente).
    pub fn all<T: DeserializeOwned>(&self, table: &str) -> std::io::Result<Vec<T>> {
        let path = self.table_path(table);
        if !path.exists() { return Ok(vec![]); }
        let f = fs::File::open(&path)?;
        let reader = BufReader::new(f);
        let mut out = Vec::new();
        for line in reader.lines() {
            let line = line?;
            if line.trim().is_empty() { continue; }
            if let Ok(v) = serde_json::from_str::<T>(&line) {
                out.push(v);
            }
        }
        Ok(out)
    }

    /// Verifica se existe registro com _id.
    pub fn exists(&self, table: &str, id: &str) -> bool {
        let path = self.table_path(table);
        if !path.exists() { return false; }
        let Ok(f) = fs::File::open(&path) else { return false; };
        let needle = format!("\"_id\":\"{}\"", id);
        BufReader::new(f).lines()
            .filter_map(|l| l.ok())
            .any(|l| l.contains(&needle))
    }

    /// Deleta por _id — reescreve com flock.
    pub fn delete(&self, table: &str, id: &str) -> std::io::Result<usize> {
        let path = self.table_path(table);
        if !path.exists() { return Ok(0); }

        // lock de escrita — abre em r+w para segurar o lock durante reescrita
        let lock_f = OpenOptions::new().read(true).write(true).open(&path)?;
        lock_ex(&lock_f);

        let all: Vec<Value> = {
            let f = fs::File::open(&path)?;
            let reader = BufReader::new(f);
            reader.lines()
                .filter_map(|l| l.ok())
                .filter(|l| !l.trim().is_empty())
                .filter_map(|l| serde_json::from_str::<Value>(&l).ok())
                .collect()
        };

        let (keep, removed): (Vec<_>, Vec<_>) = all
            .into_iter()
            .partition(|v| v.get("_id").and_then(Value::as_str) != Some(id));

        let count = removed.len();
        let mut f = fs::File::create(&path)?;
        for v in &keep {
            writeln!(f, "{}", serde_json::to_string(v).unwrap())?;
        }
        unlock(&lock_f);
        Ok(count)
    }

    /// Upsert: deleta o anterior (se existir) e insere novo.
    pub fn upsert<T: Serialize + DeserializeOwned>(&self, table: &str, id: &str, new: &T) -> std::io::Result<()> {
        let _ = self.delete(table, id)?;
        self.insert(table, new)
    }

    /// Próximo ID numérico para uma board (max atual + 1).
    pub fn next_post_id(&self, board: &str) -> u64 {
        let all: Vec<Value> = self.all("posts").unwrap_or_default();
        let prefix = format!("{}:", board);
        all.iter()
            .filter_map(|v| v.get("_id")?.as_str())
            .filter(|id| id.starts_with(&prefix))
            .filter_map(|id| id[prefix.len()..].parse::<u64>().ok())
            .max()
            .unwrap_or(0) + 1
    }

    /// Compacta: mantém só o registro mais recente por _id.
    pub fn compact(&self, table: &str) -> std::io::Result<()> {
        let path = self.table_path(table);
        if !path.exists() { return Ok(()); }
        let lock_f = OpenOptions::new().read(true).write(true).open(&path)?;
        lock_ex(&lock_f);

        let all: Vec<Value> = {
            let f = fs::File::open(&path)?;
            BufReader::new(f).lines()
                .filter_map(|l| l.ok())
                .filter(|l| !l.trim().is_empty())
                .filter_map(|l| serde_json::from_str::<Value>(&l).ok())
                .collect()
        };

        let mut seen = std::collections::HashSet::new();
        let mut deduped: Vec<&Value> = Vec::new();
        for v in all.iter().rev() {
            if let Some(id) = v.get("_id").and_then(Value::as_str) {
                if seen.insert(id.to_string()) { deduped.push(v); }
            }
        }
        deduped.reverse();

        let mut f = fs::File::create(&path)?;
        for v in deduped {
            writeln!(f, "{}", serde_json::to_string(v).unwrap())?;
        }
        unlock(&lock_f);
        Ok(())
    }
}
