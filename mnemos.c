/*
 * Mnemonyse: The Simplest Version Control
 * -------------------------------------------
 * Designed for humans who just want their files tracked and backed up.
 * Can be explained to your cat in one sitting.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>

#define MNEMOS_DIR ".mnemos"
#define INDEX_FILE ".mnemos/index"
#define OBJECTS_DIR ".mnemos/objects"
#define COMMITS_DIR ".mnemos/commits"
#define HEAD_FILE ".mnemos/HEAD"
#define HASH_SIZE 64
#define REMOTE_FILE ".mnemos/remote"
#define COMMITS_DIR ".mnemos/commits"

/**
 * MurmurHash3 simplified implementation
 */
uint32_t murmur3_32(const char *key, size_t len, uint32_t seed) {
    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;
    uint32_t r1 = 15;
    uint32_t r2 = 13;
    uint32_t m = 5;
    uint32_t n = 0xe6546b64;

    uint32_t hash = seed;

    const int nblocks = len / 4;
    const uint32_t *blocks = (const uint32_t *) key;
    int i;
    for (i = 0; i < nblocks; i++) {
        uint32_t k = blocks[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;

        hash ^= k;
        hash = (hash << r2) | (hash >> (32 - r2));
        hash = hash * m + n;
    }

    const uint8_t *tail = (const uint8_t *)(key + nblocks * 4);
    uint32_t k1 = 0;

    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16;
        case 2: k1 ^= tail[1] << 8;
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = (k1 << r1) | (k1 >> (32 - r1));
                k1 *= c2;
                hash ^= k1;
    }

    hash ^= len;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

    return hash;
}


void init();
void track(const char *filename);
void track_all();
void restore_recursive(const char *src_base, const char *dest_base);
void commit(const char *message);
void revert(const char *commit_hash);
void diff_file(const char *filename, const char *commit1, const char *commit2, int latest_flag);
void copy_file(const char *src, const char *dest);
void set_remote(const char *remote_path);
void send();
void fetch();
void create_remote(const char *remote_path);

void hash_file(const char *filename, char *hash_out) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file for hashing");
        exit(1);
    }

    char buffer[1024];
    size_t bytes_read;
    uint32_t hash = 0;

    // seed for MurmurHash
    uint32_t seed = 42;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        hash = murmur3_32(buffer, bytes_read, hash ^ seed);
    }
    fclose(file);

    // hash to string
    snprintf(hash_out, HASH_SIZE, "%08x", hash);
}

void remove_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return;

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

            char subpath[512];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            remove_recursive(subpath);
        }
        closedir(dir);
        rmdir(path);
    } else {
        unlink(path);
    }
}

// restore from commit and clean the working dir
void revert_clean(const char *commit_hash) {
    // restore commit as was
    char commit_dir[256];
    snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, commit_hash);
    struct stat st;
    if (stat(commit_dir, &st) != 0) {
        printf("Error: Commit %s not found.\n", commit_hash);
        return;
    }

    printf("Reverting to commit: %s\n", commit_hash);
    restore_recursive(commit_dir, ".");

    // clean the working dir to match commit
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to open index for cleanup");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), index)) {
        // remove newline
        line[strcspn(line, "\n")] = 0;  
        if (stat(line, &st) != 0) {
            printf("Removing: %s\n", line);
            remove_recursive(line);
        }
    }
    fclose(index);

    // update HEAD according to reverted commit
    FILE *head = fopen(HEAD_FILE, "w");
    if (!head) {
        perror("Failed to update HEAD");
        return;
    }
    fprintf(head, "%s\n", commit_hash);
    fclose(head);

    printf("Revert complete.\n");
}

// initialize with init
void init() {
    mkdir(MNEMOS_DIR, 0755);
    mkdir(OBJECTS_DIR, 0755);
    mkdir(COMMITS_DIR, 0755);

    // if file doesnt exist, create it, otherwise reset to zero bytes
    FILE *index = fopen(INDEX_FILE, "w");
    // close
    fclose(index);
    
    // if file doesnt exist, create it, otherwise reset to zero bytes
    // no existential crises here
    FILE *head = fopen(HEAD_FILE, "w");
    // close
    fclose(head);

    printf("Initialized empty mnemos repository in %s\n", MNEMOS_DIR);
}

// track file, because we care about it now
void track(const char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        printf("Error: File '%s' does not exist. Skipping.\n", filename);
        return;
    }

    // check if already tracked, we don't need duplicates
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to open index file for reading");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), index)) {
        line[strcspn(line, "\n")] = 0; // strip newline
        if (strcmp(line, filename) == 0) {
            fclose(index);
            printf("File '%s' is already tracked. Skipping.\n", filename);
            return;
        }
    }
    fclose(index);

    // add file to index, because we decided it's important
    index = fopen(INDEX_FILE, "a");
    if (!index) {
        perror("Failed to open index file for appending");
        exit(1);
    }
    fprintf(index, "%s\n", filename);
    fclose(index);

    printf("Tracking file: %s\n", filename);
}

// track everything here in current dir like you're a hoarder
void track_all_recursive(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("Failed to open directory");
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip special files and `.mnemos` internals
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strncmp(entry->d_name, ".mnemos", 7) == 0) {
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // Recurse into subdirectories
                track_all_recursive(full_path);
            } else if (S_ISREG(st.st_mode)) {
                // Track regular files
                track(full_path);
            }
        } else {
            perror("Failed to stat file");
        }
    }
    closedir(dir);
}

void track_all() {
    // recursive tracking from current dir
    track_all_recursive(".");
}
void create_directories(const char *path) {
    char temp[256];
    strncpy(temp, path, sizeof(temp));
    temp[sizeof(temp) - 1] = '\0';

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            // make way for intermediate directory
            mkdir(temp, 0755); 
            *p = '/';
        }
    }
}

// just commit, you have better things to do than read 47 pages of documentation.
void commit(const char *message) {
    char line[256];
    char file_hash[HASH_SIZE];
    FILE *temp_index;

    // generate commit ID based on curr timestamp
    time_t now = time(NULL);
    char commit_hash[HASH_SIZE];
    snprintf(commit_hash, sizeof(commit_hash), "%lx", now);

    // make way for commit directory
    char commit_dir[256];
    snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, commit_hash);
    mkdir(commit_dir, 0755);

    // save commit metadata
    char metadata_path[256];
    snprintf(metadata_path, sizeof(metadata_path), "%s/message", commit_dir);
    FILE *metadata = fopen(metadata_path, "w");
    if (!metadata) {
        perror("Failed to create commit metadata");
        exit(1);
    }
    fprintf(metadata, "message: %s\n", message);
    fclose(metadata);

    // save commit timestamp
    char timestamp_path[256];
    snprintf(timestamp_path, sizeof(timestamp_path), "%s/timestamp", commit_dir);
    FILE *timestamp = fopen(timestamp_path, "w");
    if (!timestamp) {
        perror("Failed to create timestamp file");
        exit(1);
    }
    fprintf(timestamp, "%ld\n", now);
    fclose(timestamp);

    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to read index");
        exit(1);
    }

    // temp index to track valid files for next commit
    temp_index = fopen(".mnemos/index.temp", "w");
    if (!temp_index) {
        perror("Failed to create temporary index");
        fclose(index);
        exit(1);
    }

    // for each file in index
    while (fgets(line, sizeof(line), index)) {
        // strip newline
        line[strcspn(line, "\n")] = 0; 

        struct stat st;
        if (stat(line, &st) == 0) {
            // hash file content
            hash_file(line, file_hash);

            // establish object path in objects dir
            char object_path[256];
            snprintf(object_path, sizeof(object_path), "%s/%s", OBJECTS_DIR, file_hash);

            // save hash reference in commit dir
            char commit_file_path[256];
            snprintf(commit_file_path, sizeof(commit_file_path), "%s/%s", commit_dir, line);
            create_directories(commit_file_path);

            FILE *commit_entry = fopen(commit_file_path, "w");
            if (!commit_entry) {
                perror("Failed to create commit file entry");
                fclose(index);
                fclose(temp_index);
                return;
            }
            // save file hash
            fprintf(commit_entry, "%s\n", file_hash); 
            fclose(commit_entry);

            // copy file content to objects (if it doesnt already exist)
            if (access(object_path, F_OK) == -1) {
                copy_file(line, object_path);
            }

            // add file back to next commit index
            fprintf(temp_index, "%s\n", line);
        } else {
            printf("Warning: File '%s' is missing. Skipping.\n", line);
        }
    }
    fclose(index);
    fclose(temp_index);

    // replace old index with updated index
    rename(".mnemos/index.temp", INDEX_FILE);

    // update HEAD to point to latest commit
    FILE *head = fopen(HEAD_FILE, "w");
    if (!head) {
        perror("Failed to update HEAD");
        exit(1);
    }
    fprintf(head, "%s\n", commit_hash);
    fclose(head);

    printf("Committed changes: %s\n", message);
}
// Mnemonyse remembers. 
// Recursive function to traverse and restore directories and files
void restore_recursive(const char *src_base, const char *dest_base) {
    struct stat st;
    DIR *dir = opendir(src_base);
    if (!dir) {
        perror("Failed to open source directory during revert");
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // skip special files and metadata
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, "timestamp") == 0 || strcmp(entry->d_name, "message") == 0) {
            continue;
        }

        char src_entry[512], dest_entry[512];
        snprintf(src_entry, sizeof(src_entry), "%s/%s", src_base, entry->d_name);
        snprintf(dest_entry, sizeof(dest_entry), "%s/%s", dest_base, entry->d_name);

        if (stat(src_entry, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // dir must exist
                mkdir(dest_entry, 0755);
                restore_recursive(src_entry, dest_entry); 
            } else if (S_ISREG(st.st_mode)) {
                // restore file
                FILE *hash_file = fopen(src_entry, "r");
                if (!hash_file) {
                    perror("Failed to open hash file during restore");
                    continue;
                }

                char file_hash[HASH_SIZE];
                if (fgets(file_hash, sizeof(file_hash), hash_file)) {
                    // strip newline
                    file_hash[strcspn(file_hash, "\n")] = 0; 
                }
                fclose(hash_file);

                // locate file in objects
                char object_path[512];
                snprintf(object_path, sizeof(object_path), "%s/%s", OBJECTS_DIR, file_hash);

                if (access(object_path, F_OK) != 0) {
                    printf("Error: Object %s not found for file '%s'\n", file_hash, dest_entry);
                    continue;
                }

                // restore content from objects/
                copy_file(object_path, dest_entry);
                printf("Restored file: %s\n", dest_entry);
            }
        } else {
            perror("Failed to stat source entry during revert");
        }
    }
    closedir(dir);
}
/* 
 * Mnemonyse remembers. Revert to another time, a simpler time.
 *
 * If I want to revert, let me revert! Don't nanny me about unstaged or untracked files.
 * Either:
 *   1. Handhold the "stupid masses" through every commit, rebase, and revert; OR
 *   2. Be a pure, no-frills, Unix-style tool that assumes the user knows what they're doing.
 */
void revert(const char *commit_hash) {
    char commit_dir[256];
    struct stat st;

    // does commit exist
    snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, commit_hash);
    if (stat(commit_dir, &st) != 0) {
        printf("Error: Commit %s not found.\n", commit_hash);
        exit(1);
    }

    printf("Reverting to commit: %s\n", commit_hash);

    // restore files from target commit
    restore_recursive(commit_dir, ".");

    // remove files not in the target commit
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to open index for cleanup");
        exit(1);
    }

    char line[256];
    while (fgets(line, sizeof(line), index)) {
        line[strcspn(line, "\n")] = 0; // remove newline
        snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, commit_hash);

        char commit_file_path[512];
        snprintf(commit_file_path, sizeof(commit_file_path), "%s/%s", commit_dir, line);

        // does file exist in target commit
        if (stat(commit_file_path, &st) != 0) {
            printf("Removing: %s\n", line);
            remove_recursive(line);
        }
    }
    fclose(index);

    // update HEAD
    FILE *head = fopen(HEAD_FILE, "w");
    if (!head) {
        perror("Failed to update HEAD");
        exit(1);
    }
    fprintf(head, "%s\n", commit_hash);
    fclose(head);

    printf("Revert complete.\n");
}

/*
 * moments: A simple stroll through project history.
 * 
 *   - Pretty log? Add --pretty=oneline or --graph or --decorate=auto.
 *   - Reverse order? Combine flags until you break something.
 * 
 * Mnemonyse gives you moments. Oldest? Newest? Just ask.
 * Not feeling like you are hacking Pentagon to see what you worked on last week.
 */
void moments(const char *order_flag) {
    DIR *dir = opendir(COMMITS_DIR);
    if (!dir) {
        perror("Failed to open commits directory");
        exit(1);
    }

    struct dirent *entry;
    struct {
        char hash[HASH_SIZE];
        time_t timestamp;
        char message[256];
    } commits[1024]; // make this adjustable?
    int count = 0;

    // collect commit data
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char commit_dir[256], timestamp_path[256], message_path[256];
        snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, entry->d_name);

        snprintf(timestamp_path, sizeof(timestamp_path), "%s/timestamp", commit_dir);
        snprintf(message_path, sizeof(message_path), "%s/message", commit_dir);

        FILE *timestamp_file = fopen(timestamp_path, "r");
        FILE *message_file = fopen(message_path, "r");

        if (timestamp_file) {
            fscanf(timestamp_file, "%ld", &commits[count].timestamp);
            fclose(timestamp_file);
        } else {
            commits[count].timestamp = 0;
        }

        if (message_file) {
            fgets(commits[count].message, sizeof(commits[count].message), message_file);
            commits[count].message[strcspn(commits[count].message, "\n")] = 0; // remove newline
            fclose(message_file);
        } else {
            strcpy(commits[count].message, "No message");
        }

        strcpy(commits[count].hash, entry->d_name);
        count++;
    }
    closedir(dir);

    // sort commits by timestamp
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (commits[i].timestamp > commits[j].timestamp) {
                // Swap
                time_t temp_time = commits[i].timestamp;
                commits[i].timestamp = commits[j].timestamp;
                commits[j].timestamp = temp_time;

                char temp_hash[HASH_SIZE], temp_message[256];
                strcpy(temp_hash, commits[i].hash);
                strcpy(temp_message, commits[i].message);

                strcpy(commits[i].hash, commits[j].hash);
                strcpy(commits[i].message, commits[j].message);

                strcpy(commits[j].hash, temp_hash);
                strcpy(commits[j].message, temp_message);
            }
        }
    }

    printf("Commit Moments:\n");
    if (strcmp(order_flag, "-n") == 0) {
        for (int i = count - 1; i >= 0; --i) {
            printf("Commit: %s | Time: %s | Message: %s\n",
                   commits[i].hash,
                   // human-readable time
                   ctime(&commits[i].timestamp), 
                   commits[i].message);
        }
    } else if (strcmp(order_flag, "-o") == 0) {
        for (int i = 0; i < count; ++i) {
            printf("Commit: %s | Time: %s | Message: %s\n",
                   commits[i].hash,
                   ctime(&commits[i].timestamp),
                   commits[i].message);
        }
    } else {
        printf("Invalid flag for moments. Use -n (newest) or -o (oldest).\n");
    }
}

/* 
 * Let's talk about diff.
 * 
 * Compare the index? The staging area? 
 * Your last mistake? Your future regret? Who knows.
 *   - Want to compare two commits? Provide filenames and commit hashes.
 *   - Want to compare a file to the latest commit? Use -n.
 */
void diff_file(const char *filename, const char *commit1, const char *commit2, int latest_flag) {
    char path1[512], path2[512];
    struct stat st1, st2;
    int result;

    if (latest_flag) {
        // get latest commit hash
        FILE *head_file = fopen(HEAD_FILE, "r");
        if (!head_file) {
            printf("Error: Could not open HEAD file to get the latest commit.\n");
            return;
        }

        char latest_commit[HASH_SIZE];
        if (!fgets(latest_commit, sizeof(latest_commit), head_file)) {
            printf("Error: Could not read the latest commit hash from HEAD file.\n");
            fclose(head_file);
            return;
        }
        fclose(head_file);

        // trim newline if exists
        latest_commit[strcspn(latest_commit, "\n")] = 0;

        // path to file in the latest commit
        snprintf(path1, sizeof(path1), "%s/%s/%s", COMMITS_DIR, latest_commit, filename);
        snprintf(path2, sizeof(path2), "%s", filename); // Working directory file
    } else {
        // paths to files in specified commits
        snprintf(path1, sizeof(path1), "%s/%s/%s", COMMITS_DIR, commit1, filename);
        snprintf(path2, sizeof(path2), "%s/%s/%s", COMMITS_DIR, commit2, filename);
    }

    // does file exist?
    if (stat(path1, &st1) != 0) {
        printf("Error: File '%s' does not exist in the specified commit or latest commit.\n", filename);
        return;
    }
    if (stat(path2, &st2) != 0) {
        printf("Error: File '%s' does not exist in the working directory or specified commit.\n", filename);
        return;
    }

    // execute diff command with color
    char command[1024];
    snprintf(command, sizeof(command), "diff --color=always %s %s", path1, path2);
    result = system(command);

    if (result == 0) {
        printf("Files are identical.\n");
    } else if (result == 256) { // exit code 1 translates to 256 in system
        printf("Files differ.\n");
    } else {
        printf("Error running diff command.\n");
    }
}

// copy file
void copy_file(const char *src, const char *dest) {
    FILE *source = fopen(src, "r");
    if (!source) {
        perror("Failed to open source file");
        exit(1);
    }

    FILE *destination = fopen(dest, "w");
    if (!destination) {
        perror("Failed to open destination file");
        fclose(source);
        exit(1);
    }

    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        fwrite(buffer, 1, bytes, destination);
    }

    fclose(source);
    fclose(destination);
}

// remote repository
void set_remote(const char *remote_path) {
    FILE *remote = fopen(REMOTE_FILE, "w");
    if (!remote) {
        perror("Failed to set remote");
        exit(1);
    }
    fprintf(remote, "%s\n", remote_path);
    fclose(remote);

    printf("Remote set to: %s\n", remote_path);
}

/* SEND
Fast-forward? Rebase? Merge? Detached heads? Do I look like I care? 
Maybe sacrifice a goat under a dark moon first?
we're not pushing, no babies here, just sending files like normal sane humans.
*/
void send() {
    FILE *remote = fopen(REMOTE_FILE, "r");
    if (!remote) {
        printf("No remote configured. Use 'mnemos remote <path>' to set one.\n");
        exit(1);
    }

    char remote_path[256];
    fgets(remote_path, sizeof(remote_path), remote);
    fclose(remote);

    // trim newline
    remote_path[strcspn(remote_path, "\n")] = 0;

    // user@host and path
    char remote_host[128] = {0};
    char remote_dir[128] = {0};
    char *colon = strchr(remote_path, ':');
    if (colon) {
        size_t host_len = colon - remote_path;
        strncpy(remote_host, remote_path, host_len);
        remote_host[host_len] = '\0';
        strcpy(remote_dir, colon + 1);
    } else {
        printf("Invalid remote path format. Use user@host:/path/to/repo\n");
        return;
    }

    // remote directories must be created
    char command[512];
    snprintf(command, sizeof(command),
             "ssh %s 'mkdir -p \"%s/commits\" \"%s/objects\"'",
             remote_host, remote_dir, remote_dir);
    int result_mkdir = system(command);
    if (result_mkdir != 0) {
        printf("Failed to create remote directories at %s:%s\n", remote_host, remote_dir);
        return;
    }

    // rsync commits
    snprintf(command, sizeof(command), "rsync -av %s/ %s:%s/commits/", COMMITS_DIR, remote_host, remote_dir);
    int result_commits = system(command);

    // rsync objects
    snprintf(command, sizeof(command), "rsync -av %s/ %s:%s/objects/", OBJECTS_DIR, remote_host, remote_dir);
    int result_objects = system(command);

    if (result_commits == 0 && result_objects == 0) {
        printf("Commits and objects sent to remote: %s:%s\n", remote_host, remote_dir);
    } else {
        if (result_commits != 0) {
            printf("Failed to send commits to remote.\n");
        }
        if (result_objects != 0) {
            printf("Failed to send objects to remote.\n");
        }
    }
}

// fetch commits from remote
void fetch() {
    FILE *remote = fopen(REMOTE_FILE, "r");
    if (!remote) {
        printf("No remote configured. Use 'mnemos remote <path>' to set one.\n");
        exit(1);
    }

    char remote_path[256];
    fgets(remote_path, sizeof(remote_path), remote);
    fclose(remote);

    // trim newline
    remote_path[strcspn(remote_path, "\n")] = 0;

    // .mnemos must exist
    struct stat st;
    if (stat(MNEMOS_DIR, &st) != 0) {
        printf("Error: This is not a Mnemos repository. Initialize it first with 'mnemos init'.\n");
        exit(1);
    }

    // rsync commits
    char command[512];
    snprintf(command, sizeof(command), "rsync -av %s/commits/ %s/", remote_path, COMMITS_DIR);
    int result_commits = system(command);

    // rsync objects
    snprintf(command, sizeof(command), "rsync -av %s/objects/ %s/", remote_path, OBJECTS_DIR);
    int result_objects = system(command);

    if (result_commits == 0 && result_objects == 0) {
        printf("Commits and objects fetched from remote: %s\n", remote_path);
    } else {
        if (result_commits != 0) {
            printf("Failed to fetch commits from remote.\n");
        }
        if (result_objects != 0) {
            printf("Failed to fetch objects from remote.\n");
        }
    }
}

// create remote repository from local mnemos
void create_remote(const char *remote_path) {
    char command[512];

    // single quotes around the mkdir command to so shell interprets ~ correctly
    snprintf(command, sizeof(command),
             "ssh %s 'mkdir -p \"%s/commits\" && mkdir -p \"%s/objects\" && touch \"%s/HEAD\"'",
             remote_path, remote_path, remote_path, remote_path);
    int result = system(command);

    if (result == 0) {
        printf("Created remote repository at: %s\n", remote_path);
        set_remote(remote_path); // automatically set new remote
    } else {
        printf("Failed to create remote repository.\n");
    }
}

// just init repo on remote from local
void remote_init() {
    FILE *remote = fopen(REMOTE_FILE, "r");
    if (!remote) {
        printf("No remote configured. Use 'mnemos remote <path>' to set one.\n");
        return;
    }

    char remote_path[256];
    if (!fgets(remote_path, sizeof(remote_path), remote)) {
        fclose(remote);
        printf("Failed to read remote configuration.\n");
        return;
    }
    fclose(remote);

    // trim newline
    remote_path[strcspn(remote_path, "\n")] = 0;

    // make sure remote_path is not empty
    if (strlen(remote_path) == 0) {
        printf("Remote path is empty. Please set a valid remote path.\n");
        return;
    }

    // extract user@host and remote directory
    char user_host[128] = {0};
    char remote_dir[128] = {0};
    char *colon = strchr(remote_path, ':');
    if (colon) {
        size_t host_len = colon - remote_path;
        strncpy(user_host, remote_path, host_len);
        user_host[host_len] = '\0';
        strcpy(remote_dir, colon + 1);
    } else {
        printf("Invalid remote path format. Use user@host:/path/to/repo\n");
        return;
    }

    // for SSH command
    char command[512];
    snprintf(command, sizeof(command),
             "ssh %s 'mkdir -p \"%s/commits\" && mkdir -p \"%s/objects\" && touch \"%s/HEAD\" && touch \"%s/index\"'",
             user_host, remote_dir, remote_dir, remote_dir, remote_dir);

    // debug
    // printf("Executing command: %s\n", command);

    // execute 
    int result = system(command);

    if (result == 0) {
        printf("Initialized remote repository at: %s\n", remote_path);
    } else {
        printf("Failed to initialize remote repository at: %s\n", remote_path);
    }
}

void list_commits() {
    DIR *dir = opendir(COMMITS_DIR);
    if (!dir) {
        perror("Failed to open commits directory");
        exit(1);
    }

    struct dirent *entry;
    struct {
        char hash[HASH_SIZE];
        time_t timestamp;
    } commits[1024];
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // stamp of Time for each commit, so we can see how old we get
        char timestamp_path[256];
        snprintf(timestamp_path, sizeof(timestamp_path), "%s/%s/timestamp", COMMITS_DIR, entry->d_name);

        FILE *timestamp_file = fopen(timestamp_path, "r");
        if (timestamp_file) {
            commits[count].timestamp = 0;
            fscanf(timestamp_file, "%ld", &commits[count].timestamp);
            fclose(timestamp_file);
        } else {
            commits[count].timestamp = 0; // missing timestamp
        }

        strcpy(commits[count].hash, entry->d_name);
        count++;
    }
    closedir(dir);

    // sort commits by timestamp
    for (int i = 0; i < count - 1; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (commits[i].timestamp > commits[j].timestamp) {
                // swap
                time_t temp_time = commits[i].timestamp;
                commits[i].timestamp = commits[j].timestamp;
                commits[j].timestamp = temp_time;

                char temp_hash[HASH_SIZE];
                strcpy(temp_hash, commits[i].hash);
                strcpy(commits[i].hash, commits[j].hash);
                strcpy(commits[j].hash, temp_hash);
            }
        }
    }

    // print commits
    printf("Commits (newest to oldest):\n");
    for (int i = count - 1; i >= 0; --i) {
        printf("Commit: %s, Timestamp: %ld\n", commits[i].hash, commits[i].timestamp);
    }
}


// master function
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: mnemos <command> [args]\n");
        return 1;
    }

    if (strcmp(argv[1], "init") == 0) {
        init();
    } else if (strcmp(argv[1], "track") == 0 && argc == 3) {
        if (strcmp(argv[2], "-a") == 0) {
            track_all();
        } else {
            track(argv[2]);
        }
    } else if (strcmp(argv[1], "commit") == 0 && argc == 3) {
        commit(argv[2]);
    } else if (strcmp(argv[1], "revert") == 0 && argc == 3) {
        revert(argv[2]);
    } else if (strcmp(argv[1], "remote") == 0 && argc == 3) {
        set_remote(argv[2]);
    } else if (strcmp(argv[1], "send") == 0) {
        send();
    } else if (strcmp(argv[1], "fetch") == 0) {
        fetch();
    } else if (strcmp(argv[1], "create-remote") == 0 && argc == 3) {
        create_remote(argv[2]);
    } else if (strcmp(argv[1], "remote-init") == 0) {
        remote_init();
    } else if (strcmp(argv[1], "list-commits") == 0) {
        list_commits();
    } else if (strcmp(argv[1], "moments") == 0 && argc == 3) {
        moments(argv[2]); // -n or -o
    } else if (strcmp(argv[1], "diff") == 0) {
        if (argc == 5) {
            // diff between 2 commits
            diff_file(argv[2], argv[3], argv[4], 0);
        } else if (argc == 4 && strcmp(argv[3], "-n") == 0) {
            // diff latest commit with current status - working directory
            diff_file(argv[2], NULL, NULL, 1);
        } else {
            printf("Unknown command or incorrect arguments\n");
            printf("Commands:\n");
            printf("  init                  Initialize repository\n");
            printf("  track <file>          Track a file\n");
            printf("  track -a              Track all files in the current directory\n");
            printf("  commit <msg>          Commit changes with a message\n");
            printf("  revert <hash>         Revert to a specific commit\n");
            printf("  remote <path>         Set the remote repository\n");
            printf("  send                  Send commits to the remote repository\n");
            printf("  fetch                 Fetch commits from the remote repository\n");
            printf("  create-remote <path>  Create a remote repository from scratch\n");
        }
    } else {
        printf("Unknown command or incorrect arguments\n");
    }

    return 0; 
}