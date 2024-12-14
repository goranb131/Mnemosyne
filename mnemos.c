#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#define MNEMOS_DIR ".mnemos"
#define INDEX_FILE ".mnemos/index"
#define OBJECTS_DIR ".mnemos/objects"
#define COMMITS_DIR ".mnemos/commits"
#define HEAD_FILE ".mnemos/HEAD"
#define HASH_SIZE 64
#define REMOTE_FILE ".mnemos/remote"
#define COMMITS_DIR ".mnemos/commits"

void init();
void track(const char *filename);
void track_all();
void commit(const char *message);
void revert(const char *commit_hash);
void copy_file(const char *src, const char *dest);
void set_remote(const char *remote_path);
void send();
void fetch();
void create_remote(const char *remote_path);


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
void track_all() {
    DIR *dir = opendir(".");
    if (!dir) {
        perror("Failed to open current directory");
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // skip special files
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        struct stat st;
        if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
            // track only regular files
            track(entry->d_name);
        }
    }
    closedir(dir);
}

// commit changes
void commit(const char *message) {
    char line[256];
    char hash[HASH_SIZE];

    // generate commit hash
    time_t now = time(NULL);
    snprintf(hash, sizeof(hash), "%lx", now); 

    // create commit directory
    char commit_dir[256];
    snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, hash);
    mkdir(commit_dir, 0755);

    // save metadata (commit message)
    char metadata_path[256];
    snprintf(metadata_path, sizeof(metadata_path), "%s/message", commit_dir);
    FILE *metadata = fopen(metadata_path, "w");
    if (!metadata) {
        perror("Failed to create commit metadata");
        exit(1);
    }
    fprintf(metadata, "message: %s\n", message);
    fclose(metadata);

    // save timestamp, history matters
    char timestamp_path[256];
    snprintf(timestamp_path, sizeof(timestamp_path), "%s/timestamp", commit_dir);
    FILE *timestamp = fopen(timestamp_path, "w");
    if (!timestamp) {
        perror("Failed to create timestamp file");
        exit(1);
    }
    fprintf(timestamp, "%ld\n", now); // store the epoch time
    fclose(timestamp);

    // save tracked files
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to read index");
        exit(1);
    }

    while (fgets(line, sizeof(line), index)) {
        line[strcspn(line, "\n")] = 0; // strip newline

        struct stat st;
        if (stat(line, &st) != 0) {
            printf("Warning: File '%s' is missing. Skipping.\n", line);
            continue;
        }

        char object_path[256];
        snprintf(object_path, sizeof(object_path), "%s/%s", commit_dir, line);
        copy_file(line, object_path);
    }
    fclose(index);

    // update HEAD because this is now the latest and greatest commit
    FILE *head = fopen(HEAD_FILE, "w");
    if (!head) {
        perror("Failed to update HEAD");
        exit(1);
    }
    fprintf(head, "%s\n", hash);
    fclose(head);

    printf("Committed changes: %s\n", message);
}

// Mnemos remembers. revert to another time, a simpler time
void revert(const char *commit_hash) {
    char commit_dir[256];
    char line[256];
    char tracked_files[1024][256];
    int tracked_count = 0;

    // check if commit directory exists
    snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, commit_hash);
    struct stat st;
    if (stat(commit_dir, &st) != 0) {
        printf("Error: Commit %s not found.\n", commit_hash);
        exit(1);
    }

    printf("Reverting to commit: %s\n", commit_hash);

    // read index to get the list of tracked files
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to read index");
        exit(1);
    }
    while (fgets(line, sizeof(line), index)) {
        line[strcspn(line, "\n")] = 0; // Strip newline
        strcpy(tracked_files[tracked_count++], line);
    }
    fclose(index);

    // read files in commit directory
    DIR *dir = opendir(commit_dir);
    if (!dir) {
        perror("Failed to open commit directory");
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // skip special files and metadata files
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, "timestamp") == 0 || strcmp(entry->d_name, "message") == 0) {
            continue;
        }

        // make a way for source (commit directory) and destination (working directory)
        char src_path[256], dest_path[256];
        snprintf(src_path, sizeof(src_path), "%s/%s", commit_dir, entry->d_name);
        snprintf(dest_path, sizeof(dest_path), "%s", entry->d_name);

        // restore file
        copy_file(src_path, dest_path);
        printf("Restored: %s\n", entry->d_name);

        // mark the file as restored (remove from tracked list)
        for (int i = 0; i < tracked_count; ++i) {
            if (strcmp(tracked_files[i], entry->d_name) == 0) {
                tracked_files[i][0] = '\0'; // mark as restored
            }
        }
    }
    closedir(dir);

    // remove files that are tracked but not in commit
    for (int i = 0; i < tracked_count; ++i) {
        if (tracked_files[i][0] != '\0') { // not restored
            if (remove(tracked_files[i]) == 0) {
                printf("Removed: %s\n", tracked_files[i]);
            } else {
                perror("Failed to remove file");
            }
        }
    }

    // ppdate HEAD
    FILE *head = fopen(HEAD_FILE, "w");
    if (!head) {
        perror("Failed to update HEAD");
        exit(1);
    }
    fprintf(head, "%s\n", commit_hash);
    fclose(head);

    printf("Revert complete.\n");
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

    // rsync commits into remote commits dir
    char command[512];
    snprintf(command, sizeof(command), "rsync -av %s/ %s/commits/", COMMITS_DIR, remote_path);
    int result = system(command);

    if (result == 0) {
        printf("Commits sent to remote: %s\n", remote_path);
    } else {
        printf("Failed to send commits to remote.\n");
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

    // make sure .mnemos exists
    struct stat st;
    if (stat(MNEMOS_DIR, &st) != 0) {
        printf("Error: This is not a Mnemos repository. Initialize it first with 'mnemos init'.\n");
        exit(1);
    }

    // rsync contents of remote commits directory directly into local .mnemos/commits
    char command[512];
    snprintf(command, sizeof(command), "rsync -av %s/commits/ %s/", remote_path, COMMITS_DIR);
    int result = system(command);

    if (result == 0) {
        printf("Commits fetched from remote: %s\n", remote_path);
    } else {
        printf("Failed to fetch commits from remote.\n");
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

    return 0;
}