PRAGMA page_size = 4096;
PRAGMA journal_mode = OFF;
PRAGMA synchronous = OFF;
PRAGMA locking_mode = EXCLUSIVE;
PRAGMA foreign_keys = ON;
PRAGMA application_id = 1178942534;
PRAGMA user_version = 1;

CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE package (
    id             INTEGER PRIMARY KEY CHECK (id = 1),
    name           TEXT NOT NULL,
    version        TEXT NOT NULL,
    architecture   TEXT NOT NULL,
    abi            TEXT NOT NULL,
    created_by     TEXT NOT NULL,
    format_version INTEGER NOT NULL,
    strict         INTEGER NOT NULL CHECK (strict IN (0, 1)),
    namespace      TEXT NOT NULL DEFAULT 'default'
);

CREATE TABLE objects (
    id           INTEGER PRIMARY KEY,
    namespace    TEXT NOT NULL DEFAULT 'default',
    name         TEXT NOT NULL,
    soname       TEXT,
    kind         TEXT NOT NULL CHECK (kind IN
                     ('program', 'interp', 'shared', 'plugin', 'audit',
                      'resource', 'runtime')),
    -- The object's bytes are in the file's payload region: stored_size bytes
    -- at offset (aligned to alignment), compressed with "compression";
    -- stored_size is NULL for uncompressed objects.  size and sha256
    -- describe the uncompressed object.
    offset       INTEGER NOT NULL CHECK (offset > 0),
    size         INTEGER NOT NULL CHECK (size > 0),
    compression  TEXT NOT NULL DEFAULT 'none'
                     CHECK (compression IN ('none', 'zstd')),
    stored_size  INTEGER CHECK (stored_size IS NULL OR stored_size > 0),
    alignment    INTEGER NOT NULL CHECK (alignment > 0),
    sha256       BLOB NOT NULL CHECK (length(sha256) = 32),
    UNIQUE (namespace, name)
);

CREATE TABLE dependencies (
    object_id       INTEGER NOT NULL,
    dependency_name TEXT NOT NULL,
    namespace       TEXT NOT NULL DEFAULT 'default',
    FOREIGN KEY (object_id) REFERENCES objects(id),
    UNIQUE (object_id, dependency_name)
);

CREATE TABLE aliases (
    namespace TEXT NOT NULL DEFAULT 'default',
    alias     TEXT NOT NULL,
    object_id INTEGER NOT NULL,
    FOREIGN KEY (object_id) REFERENCES objects(id),
    PRIMARY KEY (namespace, alias)
);

-- Environment variables the bootstrap sets for the application.  Values may
-- reference $FELF_ROOT, $FELF_APP and any variable of the caller's
-- environment as $NAME; $$ is a literal dollar sign.
CREATE TABLE environment (
    name  TEXT PRIMARY KEY,
    value TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE resources (
    id        INTEGER PRIMARY KEY,
    path      TEXT NOT NULL UNIQUE,
    mime_type TEXT,
    size      INTEGER NOT NULL CHECK (size >= 0),
    data      BLOB NOT NULL
);

CREATE INDEX idx_objects_soname ON objects(namespace, soname);
CREATE INDEX idx_objects_kind ON objects(kind);
CREATE INDEX idx_dependencies_name ON dependencies(namespace, dependency_name);
CREATE INDEX idx_aliases_object ON aliases(object_id);
