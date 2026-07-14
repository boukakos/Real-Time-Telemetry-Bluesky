# Real-Time Telemetry System (Bluesky Jetstream)
**Τελική Εργασία: Ενσωματωμένα Συστήματα Πραγματικού Χρόνου (2026)**

Αυτό το αποθετήριο περιέχει τον πλήρη πηγαίο κώδικα και τα δεδομένα (metrics) από την υλοποίηση ενός πολυνηματικού ενσωματωμένου συστήματος (Producer-Consumer) σε γλώσσα C. Το σύστημα εκτελέστηκε σε περιβάλλον Linux (Raspberry Pi) με σκοπό τη λήψη, κατηγοριοποίηση και καταγραφή δεδομένων σε πραγματικό χρόνο από το WebSocket API του Bluesky (Jetstream Firehose).

### Περιεχόμενα Αποθετηρίου:
* `main.c`: Ο πηγαίος κώδικας σε C (Thread synchronization με mutexes/cond_vars, WebSocket integration, Auto-Reconnect mechanism).
* `Makefile`: Οι κανόνες μεταγλώττισης για το Raspberry Pi.
* `auto_run.sh`: Το Bash script που σχεδιάστηκε για την αυτοματοποίηση της 24ωρης εκτέλεσης (sleep timers, background execution).
* `metrics_log.txt`: Το πραγματικό αρχείο CSV μεγέθους 1.8MB (86.307 εγγραφές) που συλλέχθηκε κατά τη διάρκεια ενός 24ώρου (συμπεριλαμβανομένης της επιτυχούς διαχείρισης βίαιης αποσύνδεσης δικτύου).
* `plot_metrics.py`: Το Data Science script (Python/Pandas) για την παραγωγή των διαγραμμάτων.
* `*.png`: Τα εξαγόμενα διαγράμματα (Jitter, Buffer Occupancy, CPU Usage).

### Οδηγίες Μεταγλώττισης (Compilation):
Το σύστημα απαιτεί την εγκατάσταση των βιβλιοθηκών `libwebsockets` και `cJSON`. Για τη μεταγλώττιση, απλώς εκτελέστε:
```bash
make clean
make
