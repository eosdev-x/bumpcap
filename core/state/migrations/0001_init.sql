CREATE TABLE pins (
    version TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    pinned_at INTEGER NOT NULL
);

CREATE TABLE notes (
    version TEXT PRIMARY KEY,
    note TEXT NOT NULL DEFAULT '',
    updated_at INTEGER NOT NULL
);

CREATE TABLE seen_versions (
    source_id TEXT NOT NULL,
    version TEXT NOT NULL,
    first_seen_at INTEGER NOT NULL,
    notified INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (source_id, version)
);

CREATE TABLE install_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version TEXT NOT NULL,
    source_id TEXT NOT NULL,
    action TEXT NOT NULL,
    performed_at INTEGER NOT NULL,
    success INTEGER NOT NULL
);

CREATE TABLE schema_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

