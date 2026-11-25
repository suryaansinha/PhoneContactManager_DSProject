#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_SIZE 100
#define PHONE_SIZE 32
#define EMAIL_SIZE 100
#define PHONE_BUCKETS 101
#define RECENT_LIMIT 5
#define DATA_FILE "contacts.txt"
#define TEMP_FILE "contacts.tmp"

typedef struct {
    int id;
    char name[NAME_SIZE];
    char phone[PHONE_SIZE];
    char email[EMAIL_SIZE];
} Contact;

typedef struct ContactNode {
    Contact contact;
    struct ContactNode *next;
} ContactNode;

typedef struct PhoneEntry {
    char phone[PHONE_SIZE];
    ContactNode *record;
    struct PhoneEntry *next;
} PhoneEntry;

typedef struct NameNode {
    char name[NAME_SIZE];
    int id;
    ContactNode *record;
    struct NameNode *left;
    struct NameNode *right;
} NameNode;

typedef struct QueueNode {
    int id;
    struct QueueNode *next;
} QueueNode;

typedef enum {
    ACTION_ADD,
    ACTION_DELETE,
    ACTION_UPDATE
} ActionType;

typedef struct UndoAction {
    ActionType type;
    Contact before;
    Contact after;
    struct UndoAction *next;
} UndoAction;

typedef struct {
    int next_id;
    ContactNode *contacts;
    PhoneEntry *phone_index[PHONE_BUCKETS];
    NameNode *names;
    QueueNode *recent_head;
    QueueNode *recent_tail;
    size_t recent_count;
    UndoAction *undo_top;
} ContactManager;

static int compareNames(const char *left, const char *right) {
    while (*left && *right) {
        int a = tolower((unsigned char)*left);
        int b = tolower((unsigned char)*right);
        if (a != b) return a < b ? -1 : 1;
        ++left;
        ++right;
    }
    if (*left) return 1;
    if (*right) return -1;
    return 0;
}

static int compareNameKeys(const char *left_name, int left_id,
                           const char *right_name, int right_id) {
    int result = compareNames(left_name, right_name);
    if (result != 0) return result;
    if (left_id == right_id) return 0;
    return left_id < right_id ? -1 : 1;
}

static unsigned long phoneHash(const char *phone) {
    unsigned long hash = 5381;
    while (*phone) hash = ((hash << 5) + hash) + (unsigned char)*phone++;
    return hash % PHONE_BUCKETS;
}

static ContactNode *findById(ContactManager *manager, int id) {
    ContactNode *current = manager->contacts;
    while (current) {
        if (current->contact.id == id) return current;
        current = current->next;
    }
    return NULL;
}

static PhoneEntry *findPhoneEntry(ContactManager *manager, const char *phone) {
    PhoneEntry *entry = manager->phone_index[phoneHash(phone)];
    while (entry) {
        if (strcmp(entry->phone, phone) == 0) return entry;
        entry = entry->next;
    }
    return NULL;
}

static void insertPhoneEntry(ContactManager *manager, PhoneEntry *entry) {
    unsigned long bucket = phoneHash(entry->phone);
    entry->next = manager->phone_index[bucket];
    manager->phone_index[bucket] = entry;
}

static void removePhoneEntry(ContactManager *manager, const char *phone) {
    unsigned long bucket = phoneHash(phone);
    PhoneEntry **link = &manager->phone_index[bucket];
    while (*link) {
        if (strcmp((*link)->phone, phone) == 0) {
            PhoneEntry *removed = *link;
            *link = removed->next;
            free(removed);
            return;
        }
        link = &(*link)->next;
    }
}

static void insertNameNode(NameNode **root, NameNode *node) {
    if (!*root) {
        *root = node;
    } else if (compareNameKeys(node->name, node->id, (*root)->name, (*root)->id) < 0) {
        insertNameNode(&(*root)->left, node);
    } else {
        insertNameNode(&(*root)->right, node);
    }
}

static NameNode *extractMinimum(NameNode **root) {
    if ((*root)->left) return extractMinimum(&(*root)->left);
    NameNode *minimum = *root;
    *root = minimum->right;
    minimum->right = NULL;
    return minimum;
}

static void removeNameNode(NameNode **root, const char *name, int id) {
    if (!*root) return;
    int comparison = compareNameKeys(name, id, (*root)->name, (*root)->id);
    if (comparison < 0) {
        removeNameNode(&(*root)->left, name, id);
    } else if (comparison > 0) {
        removeNameNode(&(*root)->right, name, id);
    } else {
        NameNode *removed = *root;
        if (!removed->left) {
            *root = removed->right;
            free(removed);
        } else if (!removed->right) {
            *root = removed->left;
            free(removed);
        } else {
            NameNode *successor = extractMinimum(&removed->right);
            successor->left = removed->left;
            successor->right = removed->right;
            *root = successor;
            free(removed);
        }
    }
}

static int pushUndo(ContactManager *manager, ActionType type,
                    const Contact *before, const Contact *after) {
    UndoAction *action = (UndoAction *)malloc(sizeof(UndoAction));
    if (!action) return 0;
    action->type = type;
    if (before) action->before = *before;
    else memset(&action->before, 0, sizeof(action->before));
    if (after) action->after = *after;
    else memset(&action->after, 0, sizeof(action->after));
    action->next = manager->undo_top;
    manager->undo_top = action;
    return 1;
}

static int enqueueRecent(ContactManager *manager, int id) {
    QueueNode *node = (QueueNode *)malloc(sizeof(QueueNode));
    if (!node) return 0;
    node->id = id;
    node->next = NULL;
    if (manager->recent_tail) manager->recent_tail->next = node;
    else manager->recent_head = node;
    manager->recent_tail = node;
    ++manager->recent_count;

    if (manager->recent_count > RECENT_LIMIT) {
        QueueNode *oldest = manager->recent_head;
        manager->recent_head = oldest->next;
        if (!manager->recent_head) manager->recent_tail = NULL;
        free(oldest);
        --manager->recent_count;
    }
    return 1;
}

/* Inserts one contact and builds all indexes. Used by add and file loading. */
static int storeContact(ContactManager *manager, const Contact *contact,
                        int track_recent, int record_undo) {
    ContactNode *record;
    PhoneEntry *phone_entry;
    NameNode *name_node;
    QueueNode *recent_node = NULL;
    UndoAction *undo_action = NULL;

    if (findById(manager, contact->id) || findPhoneEntry(manager, contact->phone)) return 0;

    record = (ContactNode *)malloc(sizeof(ContactNode));
    phone_entry = (PhoneEntry *)malloc(sizeof(PhoneEntry));
    name_node = (NameNode *)malloc(sizeof(NameNode));
    if (track_recent) recent_node = (QueueNode *)malloc(sizeof(QueueNode));
    if (record_undo) undo_action = (UndoAction *)malloc(sizeof(UndoAction));
    if (!record || !phone_entry || !name_node ||
        (track_recent && !recent_node) || (record_undo && !undo_action)) {
        free(record);
        free(phone_entry);
        free(name_node);
        free(recent_node);
        free(undo_action);
        return -1;
    }

    record->contact = *contact;
    record->next = manager->contacts;
    manager->contacts = record;

    strcpy(phone_entry->phone, contact->phone);
    phone_entry->record = record;
    insertPhoneEntry(manager, phone_entry);

    strcpy(name_node->name, contact->name);
    name_node->id = contact->id;
    name_node->record = record;
    name_node->left = NULL;
    name_node->right = NULL;
    insertNameNode(&manager->names, name_node);

    if (track_recent) {
        recent_node->id = contact->id;
        recent_node->next = NULL;
        if (manager->recent_tail) manager->recent_tail->next = recent_node;
        else manager->recent_head = recent_node;
        manager->recent_tail = recent_node;
        ++manager->recent_count;
        if (manager->recent_count > RECENT_LIMIT) {
            QueueNode *oldest = manager->recent_head;
            manager->recent_head = oldest->next;
            if (!manager->recent_head) manager->recent_tail = NULL;
            free(oldest);
            --manager->recent_count;
        }
    }

    if (record_undo) {
        undo_action->type = ACTION_ADD;
        memset(&undo_action->before, 0, sizeof(undo_action->before));
        undo_action->after = *contact;
        undo_action->next = manager->undo_top;
        manager->undo_top = undo_action;
    }
    if (contact->id >= manager->next_id) manager->next_id = contact->id + 1;
    return 1;
}

static int removeContact(ContactManager *manager, int id, int record_undo) {
    ContactNode **link = &manager->contacts;
    while (*link && (*link)->contact.id != id) link = &(*link)->next;
    if (!*link) return 0;

    ContactNode *record = *link;
    Contact copy = record->contact;
    UndoAction *action = NULL;
    if (record_undo) {
        action = (UndoAction *)malloc(sizeof(UndoAction));
        if (!action) return -1;
        action->type = ACTION_DELETE;
        action->before = copy;
        memset(&action->after, 0, sizeof(action->after));
        action->next = manager->undo_top;
    }

    removeNameNode(&manager->names, copy.name, copy.id);
    removePhoneEntry(manager, copy.phone);
    *link = record->next;
    free(record);
    if (action) manager->undo_top = action;
    return 1;
}

static int updateContact(ContactManager *manager, int id, const Contact *updated,
                         int record_undo) {
    ContactNode *record = findById(manager, id);
    if (!record) return 0;
    if (strcmp(record->contact.phone, updated->phone) != 0 &&
        findPhoneEntry(manager, updated->phone)) return 0;

    NameNode *name_node = (NameNode *)malloc(sizeof(NameNode));
    PhoneEntry *phone_entry = NULL;
    UndoAction *action = NULL;
    if (strcmp(record->contact.phone, updated->phone) != 0)
        phone_entry = (PhoneEntry *)malloc(sizeof(PhoneEntry));
    if (record_undo) action = (UndoAction *)malloc(sizeof(UndoAction));
    if (!name_node || (strcmp(record->contact.phone, updated->phone) != 0 && !phone_entry) ||
        (record_undo && !action)) {
        free(name_node);
        free(phone_entry);
        free(action);
        return -1;
    }

    Contact previous = record->contact;
    removeNameNode(&manager->names, previous.name, previous.id);
    if (strcmp(previous.phone, updated->phone) != 0) removePhoneEntry(manager, previous.phone);
    record->contact = *updated;

    strcpy(name_node->name, updated->name);
    name_node->id = updated->id;
    name_node->record = record;
    name_node->left = NULL;
    name_node->right = NULL;
    insertNameNode(&manager->names, name_node);

    if (phone_entry) {
        strcpy(phone_entry->phone, updated->phone);
        phone_entry->record = record;
        insertPhoneEntry(manager, phone_entry);
    }
    if (action) {
        action->type = ACTION_UPDATE;
        action->before = previous;
        action->after = *updated;
        action->next = manager->undo_top;
        manager->undo_top = action;
    }
    return 1;
}

static void printContact(const Contact *contact) {
    printf("ID: %d | Name: %s | Phone: %s", contact->id, contact->name, contact->phone);
    if (contact->email[0]) printf(" | Email: %s", contact->email);
    putchar('\n');
}

static void displayAlphabetically(const NameNode *node) {
    if (!node) return;
    displayAlphabetically(node->left);
    printContact(&node->record->contact);
    displayAlphabetically(node->right);
}

static void searchName(const NameNode *node, const char *name, size_t *matches) {
    if (!node) return;
    int comparison = compareNames(node->name, name);
    if (comparison >= 0) searchName(node->left, name, matches);
    if (comparison == 0) {
        printContact(&node->record->contact);
        ++*matches;
    }
    if (comparison <= 0) searchName(node->right, name, matches);
}

static ContactNode *findNameId(const NameNode *node, const char *name, int id) {
    if (!node) return NULL;
    int comparison = compareNames(name, node->name);
    if (comparison < 0) return findNameId(node->left, name, id);
    if (comparison > 0) return findNameId(node->right, name, id);
    if (node->id == id) return node->record;
    ContactNode *found = findNameId(node->left, name, id);
    return found ? found : findNameId(node->right, name, id);
}

static ContactNode *findAnyName(const NameNode *node, const char *name) {
    if (!node) return NULL;
    int comparison = compareNames(name, node->name);
    if (comparison == 0) return node->record;
    if (comparison < 0) return findAnyName(node->left, name);
    return findAnyName(node->right, name);
}

static int undoLastAction(ContactManager *manager) {
    UndoAction *action = manager->undo_top;
    int result;
    if (!action) return 0;

    if (action->type == ACTION_ADD) {
        result = removeContact(manager, action->after.id, 0);
    } else if (action->type == ACTION_DELETE) {
        result = storeContact(manager, &action->before, 0, 0);
    } else {
        result = updateContact(manager, action->before.id, &action->before, 0);
    }
    if (result != 1) return -1;

    manager->undo_top = action->next;
    free(action);
    return 1;
}

static int hasTab(const char *text) {
    return strchr(text, '\t') != NULL;
}

static int saveContacts(const ContactManager *manager) {
    FILE *file = fopen(TEMP_FILE, "w");
    if (!file) return 0;
    const ContactNode *current = manager->contacts;
    while (current) {
        const Contact *contact = &current->contact;
        if (fprintf(file, "%d\t%s\t%s\t%s\n", contact->id, contact->name,
                    contact->phone, contact->email) < 0) {
            fclose(file);
            remove(TEMP_FILE);
            return 0;
        }
        current = current->next;
    }
    if (fclose(file) != 0) {
        remove(TEMP_FILE);
        return 0;
    }
    if (rename(TEMP_FILE, DATA_FILE) != 0) {
        remove(TEMP_FILE);
        return 0;
    }
    return 1;
}

static int parseContactLine(char *line, Contact *contact) {
    char *first = strchr(line, '\t');
    char *second;
    char *third;
    char *end;
    long id;
    if (!first) return 0;
    *first++ = '\0';
    second = strchr(first, '\t');
    if (!second) return 0;
    *second++ = '\0';
    third = strchr(second, '\t');
    if (!third) return 0;
    *third++ = '\0';
    if (strchr(third, '\t')) return 0;

    errno = 0;
    id = strtol(line, &end, 10);
    if (errno || end == line || *end || id <= 0 || id >= 2147483647L) return 0;
    if (strlen(first) == 0 || strlen(first) >= NAME_SIZE ||
        strlen(second) == 0 || strlen(second) >= PHONE_SIZE ||
        strlen(third) >= EMAIL_SIZE) return 0;

    contact->id = (int)id;
    strcpy(contact->name, first);
    strcpy(contact->phone, second);
    strcpy(contact->email, third);
    return 1;
}

static int loadContacts(ContactManager *manager, size_t *loaded, size_t *skipped) {
    FILE *file = fopen(DATA_FILE, "r");
    char line[NAME_SIZE + PHONE_SIZE + EMAIL_SIZE + 32];
    *loaded = 0;
    *skipped = 0;
    if (!file) return errno == ENOENT ? 1 : 0;

    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        Contact contact;
        int stored;
        if (length && line[length - 1] == '\n') line[--length] = '\0';
        if (length && line[length - 1] == '\r') line[--length] = '\0';
        if (!feof(file) && length == sizeof(line) - 1 && line[length - 1] != '\n') {
            int character;
            while ((character = fgetc(file)) != '\n' && character != EOF) {}
            ++*skipped;
            continue;
        }
        if (!parseContactLine(line, &contact)) {
            ++*skipped;
            continue;
        }
        stored = storeContact(manager, &contact, 0, 0);
        if (stored == 1) ++*loaded;
        else ++*skipped;
    }
    if (ferror(file)) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

static int readLine(const char *prompt, char *output, size_t capacity) {
    size_t length;
    int character;
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(output, (int)capacity, stdin)) return 0;
    length = strlen(output);
    if (length && output[length - 1] == '\n') {
        output[--length] = '\0';
    } else {
        character = getchar();
        if (character != '\n' && character != EOF) {
            while (character != '\n' && character != EOF) character = getchar();
            printf("Input is too long. Please try again.\n");
            return -1;
        }
    }
    if (length && output[length - 1] == '\r') output[length - 1] = '\0';
    return 1;
}

static int parsePositiveInt(const char *text, int *value) {
    char *end;
    long parsed;
    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno || end == text || *end || parsed < 0 || parsed > 2147483647L) return 0;
    *value = (int)parsed;
    return 1;
}

static void freeNameTree(NameNode *node) {
    if (!node) return;
    freeNameTree(node->left);
    freeNameTree(node->right);
    free(node);
}

static void freeManager(ContactManager *manager) {
    ContactNode *contact = manager->contacts;
    while (contact) {
        ContactNode *next = contact->next;
        free(contact);
        contact = next;
    }
    for (size_t i = 0; i < PHONE_BUCKETS; ++i) {
        PhoneEntry *entry = manager->phone_index[i];
        while (entry) {
            PhoneEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    freeNameTree(manager->names);
    while (manager->recent_head) {
        QueueNode *next = manager->recent_head->next;
        free(manager->recent_head);
        manager->recent_head = next;
    }
    while (manager->undo_top) {
        UndoAction *next = manager->undo_top->next;
        free(manager->undo_top);
        manager->undo_top = next;
    }
}

int main(void) {
    ContactManager manager = {0};
    size_t loaded = 0;
    size_t skipped = 0;
    char choice[16];
    manager.next_id = 1;

    if (!loadContacts(&manager, &loaded, &skipped)) {
        printf("Could not read %s. Starting with an empty contact list.\n", DATA_FILE);
    } else {
        printf("Loaded %zu contact(s) from %s.\n", loaded, DATA_FILE);
        if (skipped) printf("Skipped %zu invalid or duplicate row(s).\n", skipped);
    }

    for (;;) {
        int read_status;
        printf("\nPhone Contact Manager\n"
               "1. Add contact\n"
               "2. View all contacts (alphabetical)\n"
               "3. Search by phone\n"
               "4. Search by exact name\n"
               "5. Update contact\n"
               "6. Delete contact by name\n"
               "7. View recently added\n"
               "8. Undo last action\n"
               "0. Save and exit\n");
        read_status = readLine("Choose an option: ", choice, sizeof(choice));
        if (read_status == 0 || (read_status == 1 && strcmp(choice, "0") == 0)) break;
        if (read_status < 0) continue;

        if (strcmp(choice, "1") == 0) {
            Contact contact = {0};
            int status;
            if (manager.next_id >= INT_MAX) {
                printf("The contact ID limit has been reached.\n");
                continue;
            }
            contact.id = manager.next_id;
            if (readLine("Name: ", contact.name, sizeof(contact.name)) != 1 ||
                readLine("Phone: ", contact.phone, sizeof(contact.phone)) != 1 ||
                readLine("Email (optional): ", contact.email, sizeof(contact.email)) != 1) continue;
            if (!contact.name[0] || !contact.phone[0] || hasTab(contact.name) ||
                hasTab(contact.phone) || hasTab(contact.email)) {
                printf("Name and phone are required; tabs are not allowed.\n");
                continue;
            }
            status = storeContact(&manager, &contact, 1, 1);
            if (status == 0) printf("That phone number is already in use.\n");
            else if (status < 0) printf("Not enough memory to add the contact.\n");
            else if (saveContacts(&manager)) printf("Contact added and saved.\n");
            else printf("Contact added, but could not save %s.\n", DATA_FILE);
        } else if (strcmp(choice, "2") == 0) {
            if (!manager.contacts) printf("No contacts saved.\n");
            else displayAlphabetically(manager.names);
        } else if (strcmp(choice, "3") == 0) {
            char phone[PHONE_SIZE];
            PhoneEntry *entry;
            if (readLine("Phone: ", phone, sizeof(phone)) != 1) continue;
            entry = findPhoneEntry(&manager, phone);
            if (entry) printContact(&entry->record->contact);
            else printf("No matching contact found.\n");
        } else if (strcmp(choice, "4") == 0) {
            char name[NAME_SIZE];
            size_t matches = 0;
            if (readLine("Exact name: ", name, sizeof(name)) != 1) continue;
            searchName(manager.names, name, &matches);
            if (!matches) printf("No matching contact found.\n");
        } else if (strcmp(choice, "5") == 0) {
            char current_phone[PHONE_SIZE];
            char new_name[NAME_SIZE];
            char new_phone[PHONE_SIZE];
            char new_email[EMAIL_SIZE];
            PhoneEntry *entry;
            Contact updated;
            int status;
            if (readLine("Current phone number: ", current_phone, sizeof(current_phone)) != 1) continue;
            entry = findPhoneEntry(&manager, current_phone);
            if (!entry) {
                printf("No matching contact found.\n");
                continue;
            }
            if (readLine("New name: ", new_name, sizeof(new_name)) != 1 ||
                readLine("New phone: ", new_phone, sizeof(new_phone)) != 1 ||
                readLine("New email (optional): ", new_email, sizeof(new_email)) != 1) continue;
            if (!new_name[0] || !new_phone[0] || hasTab(new_name) ||
                hasTab(new_phone) || hasTab(new_email)) {
                printf("Name and phone are required; tabs are not allowed.\n");
                continue;
            }
            updated = entry->record->contact;
            strcpy(updated.name, new_name);
            strcpy(updated.phone, new_phone);
            strcpy(updated.email, new_email);
            status = updateContact(&manager, updated.id, &updated, 1);
            if (status == 0) printf("That new phone number is already in use.\n");
            else if (status < 0) printf("Not enough memory to update the contact.\n");
            else if (saveContacts(&manager)) printf("Contact updated and saved.\n");
            else printf("Contact updated, but could not save %s.\n", DATA_FILE);
        } else if (strcmp(choice, "6") == 0) {
            char name[NAME_SIZE];
            size_t matches = 0;
            int status;
            if (readLine("Name to delete: ", name, sizeof(name)) != 1) continue;
            searchName(manager.names, name, &matches);
            if (!matches) {
                printf("No matching contact found.\n");
                continue;
            }
            if (matches > 1) {
                char id_text[16];
                int id;
                if (readLine("Several contacts match. Enter the ID to delete (0 to cancel): ",
                             id_text, sizeof(id_text)) != 1) continue;
                if (!parsePositiveInt(id_text, &id) || id == 0) {
                    printf("Delete cancelled or invalid ID.\n");
                    continue;
                }
                ContactNode *selected = findNameId(manager.names, name, id);
                if (!selected) {
                    printf("That ID does not match the entered name.\n");
                    continue;
                }
                status = removeContact(&manager, id, 1);
            } else {
                ContactNode *selected = findAnyName(manager.names, name);
                status = selected ? removeContact(&manager, selected->contact.id, 1) : 0;
            }
            if (status < 0) printf("Not enough memory to record this action for undo.\n");
            else if (status == 0) printf("Could not delete that contact.\n");
            else if (saveContacts(&manager)) printf("Contact deleted and saved.\n");
            else printf("Contact deleted, but could not save %s.\n", DATA_FILE);
        } else if (strcmp(choice, "7") == 0) {
            QueueNode *recent = manager.recent_head;
            int printed = 0;
            while (recent) {
                ContactNode *record = findById(&manager, recent->id);
                if (record) {
                    printContact(&record->contact);
                    printed = 1;
                }
                recent = recent->next;
            }
            if (!printed) printf("No recent contacts in this session.\n");
        } else if (strcmp(choice, "8") == 0) {
            int status = undoLastAction(&manager);
            if (status == 0) printf("There is no action to undo.\n");
            else if (status < 0) printf("Undo failed because of insufficient memory. Try again.\n");
            else if (saveContacts(&manager)) printf("Last action undone and saved.\n");
            else printf("Last action undone, but could not save %s.\n", DATA_FILE);
        } else {
            printf("Please choose a listed option.\n");
        }
    }

    if (saveContacts(&manager)) printf("Contacts saved to %s.\n", DATA_FILE);
    else printf("Could not save contacts to %s.\n", DATA_FILE);
    freeManager(&manager);
    return 0;
}
