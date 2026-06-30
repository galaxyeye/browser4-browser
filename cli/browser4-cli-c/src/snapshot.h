#ifndef BROWSER4_CLI_SNAPSHOT_H
#define BROWSER4_CLI_SNAPSHOT_H

/* Generate a timestamped filename (e.g. snapshot-2026-01-15T10-30-00Z.yml) */
char *timestamped_filename(const char *prefix, const char *ext);

/* Resolve the output path for a snapshot or screenshot */
char *resolve_output_path(const char *filename, const char *prefix, const char *ext);

/* Save text content to disk, then rotate old snapshots */
int save_snapshot(const char *path, const char *content);

/* Save binary data to disk */
int save_binary(const char *path, const unsigned char *data, size_t len);

/* Decode a base64 string to binary. Caller must free the returned buffer. */
unsigned char *base64_decode(const char *input, size_t *out_len);

#endif /* BROWSER4_CLI_SNAPSHOT_H */
