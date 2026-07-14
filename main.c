#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <libwebsockets.h>
#include <cjson/cJSON.h>

#define BUFFER_SIZE 200

// ==========================================
// ΔΟΜΕΣ ΔΕΔΟΜΕΝΩΝ & ΣΥΓΧΡΟΝΙΣΜΟΣ
// ==========================================
// Κυκλικός Buffer (Bounded Circular Queue)
char *buffer[BUFFER_SIZE];
int head = 0;
int tail = 0;
int count = 0;

// Mutex και Condition Variables για αποφυγή Busy Waiting
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;

// Καθολικοί Μετρητές (Global Counters)
int commit_count = 0;
int identity_count = 0;
int account_count = 0;
int info_count = 0;

// Flag για το Network Resilience (Auto-Reconnect)
int websocket_connected = 0;
struct lws_context *context = NULL;

// Μεταβλητές για τον υπολογισμό της CPU (/proc/stat)
unsigned long long prev_total = 0;
unsigned long long prev_idle = 0;

// Συνάρτηση υπολογισμού χρήσης CPU (%)
double get_cpu_usage() {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;
    char str[10];
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    fscanf(fp, "%s %llu %llu %llu %llu %llu %llu %llu %llu", 
           str, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(fp);

    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long total_diff = total - prev_total;
    unsigned long long idle_diff = idle - prev_idle;

    double cpu_usage = (total_diff == 0) ? 0.0 : (double)(total_diff - idle_diff) / total_diff * 100.0;

    prev_total = total;
    prev_idle = idle;
    return cpu_usage;
}

// ==========================================
// ΝΗΜΑ 2: Ο Καταναλωτής (Consumer - Event-Driven)
// ==========================================
void *consumer_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&lock);
        
        // Αναμονή (sleep) αν ο buffer είναι άδειος, μέσω condition variable
        while (count == 0) {
            pthread_cond_wait(&not_empty, &lock);
        }

        // Εξαγωγή δεδομένων από τον buffer
        char *msg = buffer[tail];
        tail = (tail + 1) % BUFFER_SIZE;
        count--;
        
        // Ειδοποίηση στον Παραγωγό ότι άδειασε μια θέση
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&lock);

        // Parsing του JSON με τη βιβλιοθήκη cJSON
        cJSON *json = cJSON_Parse(msg);
        if (json) {
            cJSON *kind = cJSON_GetObjectItemCaseSensitive(json, "kind");
            if (cJSON_IsString(kind) && (kind->valuestring != NULL)) {
                
                // Κλείδωμα για προστασία των global μετρητών
                pthread_mutex_lock(&lock);
                if (strcmp(kind->valuestring, "commit") == 0) commit_count++;
                else if (strcmp(kind->valuestring, "identity") == 0) identity_count++;
                else if (strcmp(kind->valuestring, "account") == 0) account_count++;
                else info_count++;
                pthread_mutex_unlock(&lock);
            }
            cJSON_Delete(json);
        }
        
        // Απελευθέρωση μνήμης για αποφυγή Memory Leaks!
        free(msg); 
    }
    return NULL;
}

// ==========================================
// ΝΗΜΑ 3: Ο Καταγραφέας / Monitor (Synchronous)
// ==========================================
void *monitor_thread(void *arg) {
    FILE *log_file = fopen("metrics_log.txt", "a");
    if (log_file) {
        // Εκτύπωση αυστηρού CSV header
        fprintf(log_file, "Seconds,Nanoseconds,Commit_Count,Identity_Count,Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct\n");
        fflush(log_file);
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    while (1) {
        // Ακριβής χρονισμός κάθε 1 δευτερόλεπτο (αποφυγή Jitter/Drift)
        ts.tv_sec += 1;
        clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);

        pthread_mutex_lock(&lock);
        int cur_commit = commit_count;
        int cur_identity = identity_count;
        int cur_account = account_count;
        int cur_info = info_count;
        int cur_count = count;

        // Μηδενισμός μετρητών για το επόμενο δευτερόλεπτο
        commit_count = 0;
        identity_count = 0;
        account_count = 0;
        info_count = 0;
        pthread_mutex_unlock(&lock);

        // Υπολογισμός μετρικών %
        double buffer_pct = ((double)cur_count / BUFFER_SIZE) * 100.0;
        double cpu_pct = get_cpu_usage();

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        // Εγγραφή στο αρχείο CSV (όχι στην οθόνη για λόγους απόδοσης)
        if (log_file) {
            fprintf(log_file, "%ld,%ld,%d,%d,%d,%d,%.2f,%.2f\n",
                    now.tv_sec, now.tv_nsec, cur_commit, cur_identity, cur_account, cur_info, buffer_pct, cpu_pct);
            fflush(log_file);
        }
    }
    return NULL;
}

// ==========================================
// ΝΗΜΑ 1: Ο Παραγωγός (Producer / WebSocket)
// ==========================================
static int callback_jetstream(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            websocket_connected = 1;
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            if (in && len > 0) {
                // Δέσμευση μνήμης για το JSON
                char *msg = malloc(len + 1);
                if (msg) {
                    memcpy(msg, in, len);
                    msg[len] = '\0';

                    pthread_mutex_lock(&lock);
                    if (count < BUFFER_SIZE) {
                        buffer[head] = msg;
                        head = (head + 1) % BUFFER_SIZE;
                        count++;
                        pthread_cond_signal(&not_empty); // Αφύπνιση Καταναλωτή
                    } else {
                        // Αν γεμίσει ο buffer, ρίχνουμε το πακέτο για να μη μπλοκάρει το νήμα
                        free(msg); 
                    }
                    pthread_mutex_unlock(&lock);
                }
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        case LWS_CALLBACK_CLIENT_CLOSED:
            websocket_connected = 0;
            break;
            
        default:
            break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    { "jetstream-protocol", callback_jetstream, 0, 65536 },
    { NULL, NULL, 0, 0 }
};

int main(void) {
    // Εκκίνηση των νημάτων
    pthread_t cons_tid, mon_tid;
    pthread_create(&cons_tid, NULL, consumer_thread, NULL);
    pthread_create(&mon_tid, NULL, monitor_thread, NULL);

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Η λούπα του Network Resilience (Auto-Reconnect)
    while (1) {
        context = lws_create_context(&info);
        if (!context) {
            sleep(5);
            continue;
        }

        struct lws_client_connect_info ccinfo;
        memset(&ccinfo, 0, sizeof(ccinfo));
        ccinfo.context = context;
        ccinfo.address = "jetstream1.us-east.bsky.network";
        ccinfo.port = 443;
        ccinfo.path = "/subscribe?wantedCollections=app.bsky.feed.post";
        ccinfo.host = ccinfo.address;
        ccinfo.origin = ccinfo.address;
        ccinfo.ssl_connection = LCCSCF_USE_SSL;

        struct lws *wsi = lws_client_connect_via_info(&ccinfo);
        if (!wsi) {
            lws_context_destroy(context);
            sleep(5); // Αν δεν υπάρχει δίκτυο, περιμένουμε 5 δευτερόλεπτα και ξαναπροσπαθούμε
            continue;
        }

        websocket_connected = 1;
        
        // Το Event Loop της libwebsockets
        while (websocket_connected) {
            lws_service(context, 250);
        }
        
        // Αν χάσουμε τη σύνδεση βίαια, καταστρέφουμε το context και περιμένουμε
        lws_context_destroy(context);
        sleep(5);
    }

    return 0;
}