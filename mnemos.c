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
        line[strcspn(line, "\n")] = 0; // Strip newline
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
    snprintf(hash, sizeof(hash), "%lx", now); // timestamp-based hash

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

    // save tracked files
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to read index");
        exit(1);
    }

    while (fgets(line, sizeof(line), index)) {
        line[strcspn(line, "\n")] = 0; // strip newline
        char object_path[256];
        snprintf(object_path, sizeof(object_path), "%s/%s", commit_dir, line);
        copy_file(line, object_path);
    }
    fclose(index);

    // update HEAD
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
    char line[256];
    char commit_dir[256];

    // check if commit directory exists
    snprintf(commit_dir, sizeof(commit_dir), "%s/%s", COMMITS_DIR, commit_hash);
    struct stat st;
    if (stat(commit_dir, &st) != 0) {
        printf("Error: Commit %s not found.\n", commit_hash);
        exit(1);
    }

    printf("Reverting to commit: %s\n", commit_hash);

    // read tracked files from index
    FILE *index = fopen(INDEX_FILE, "r");
    if (!index) {
        perror("Failed to read index");
        exit(1);
    }

    while (fgets(line, sizeof(line), index)) {
        line[strcspn(line, "\n")] = 0; // strip newline

        // check if file exists in the commit directory
        char object_path[256];
        snprintf(object_path, sizeof(object_path), "%s/%s", commit_dir, line);

        if (stat(object_path, &st) == 0) {
            // file exists in this commit, restore it
            copy_file(object_path, line);
            printf("Restored: %s\n", line);
        } else {
            // remove it if it doesnt belong here
            if (remove(line) == 0) {
                printf("Removed: %s\n", line);
            } else {
                perror("Failed to remove file");
            }
        }
    }
    fclose(index);

    // update HEAD, no fast-forward required
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

    char command[512];
    snprintf(command, sizeof(command), "rsync -av %s/ %s/", COMMITS_DIR, remote_path);
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

    char command[512];
    snprintf(command, sizeof(command), "rsync -av %s/ %s/", remote_path, COMMITS_DIR);
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
    snprintf(command, sizeof(command), 
             "ssh %s 'mkdir -p %s/commits && mkdir -p %s/objects && mkdir -p %s/index && touch %s/HEAD'", 
             remote_path, remote_path, remote_path, remote_path, remote_path);
    int result = system(command);

    if (result == 0) {
        printf("Created remote repository at: %s\n", remote_path);
        set_remote(remote_path); // automatically set the new remote
    } else {
        printf("Failed to create remote repository.\n");
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