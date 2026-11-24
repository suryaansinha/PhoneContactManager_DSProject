#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <queue>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

struct Contact {
    int id{};
    string name;
    string phone;
    string email;
};

class ContactManager {
private:
    struct NameKey {
        string name;
        int id{};
    };

    struct NameNode {
        NameKey key;
        unique_ptr<NameNode> left;
        unique_ptr<NameNode> right;

        explicit NameNode(NameKey value) : key(std::move(value)) {}
    };

    enum class ActionType { ADD, DELETE, UPDATE };

    struct UndoAction {
        ActionType type;
        Contact before;
        Contact after;
    };

    static constexpr size_t RECENT_LIMIT = 5;

    int nextId_ = 1;
    unordered_map<int, Contact> contactsById_;
    unordered_map<string, int> idByPhone_;
    unique_ptr<NameNode> namesRoot_;
    queue<int> recentlyAdded_;
    stack<UndoAction> undoStack_;

    void eraseContactById(int id) {
        auto found = contactsById_.find(id);
        if (found == contactsById_.end()) {
            return;
        }
        const Contact contact = found->second;
        eraseName(namesRoot_, NameKey{contact.name, contact.id});
        idByPhone_.erase(contact.phone);
        contactsById_.erase(found);
    }

    void restoreContact(const Contact& contact) {
        contactsById_[contact.id] = contact;
        idByPhone_[contact.phone] = contact.id;
        insertName(namesRoot_, NameKey{contact.name, contact.id});
    }

    void replaceContact(const Contact& contact) {
        auto found = contactsById_.find(contact.id);
        if (found == contactsById_.end()) {
            return;
        }
        const Contact oldContact = found->second;
        eraseName(namesRoot_, NameKey{oldContact.name, oldContact.id});
        idByPhone_.erase(oldContact.phone);
        found->second = contact;
        idByPhone_[contact.phone] = contact.id;
        insertName(namesRoot_, NameKey{contact.name, contact.id});
    }

    static bool keyLess(const NameKey& a, const NameKey& b) {
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.id < b.id;
    }

    static void insertName(unique_ptr<NameNode>& node, NameKey key) {
        if (!node) {
            node = make_unique<NameNode>(std::move(key));
        } else if (keyLess(key, node->key)) {
            insertName(node->left, std::move(key));
        } else {
            insertName(node->right, std::move(key));
        }
    }

    static NameKey takeMinimum(unique_ptr<NameNode>& node) {
        if (node->left) {
            return takeMinimum(node->left);
        }
        NameKey minimum = node->key;
        node = std::move(node->right);
        return minimum;
    }

    static void eraseName(unique_ptr<NameNode>& node, const NameKey& key) {
        if (!node) {
            return;
        }
        if (keyLess(key, node->key)) {
            eraseName(node->left, key);
        } else if (keyLess(node->key, key)) {
            eraseName(node->right, key);
        } else if (!node->left) {
            node = std::move(node->right);
        } else if (!node->right) {
            node = std::move(node->left);
        } else {
            node->key = takeMinimum(node->right);
        }
    }

    void collectAlphabetically(const NameNode* node, vector<Contact>& result) const {
        if (!node) {
            return;
        }
        collectAlphabetically(node->left.get(), result);
        auto found = contactsById_.find(node->key.id);
        if (found != contactsById_.end()) {
            result.push_back(found->second);
        }
        collectAlphabetically(node->right.get(), result);
    }

    void collectByName(const NameNode* node, const string& name,
                       vector<Contact>& result) const {
        if (!node) {
            return;
        }
        if (node->key.name >= name) {
            collectByName(node->left.get(), name, result);
        }
        if (node->key.name == name) {
            auto found = contactsById_.find(node->key.id);
            if (found != contactsById_.end()) {
                result.push_back(found->second);
            }
        }
        if (node->key.name <= name) {
            collectByName(node->right.get(), name, result);
        }
    }

    static void printContact(const Contact& contact) {
        cout << "ID: " << contact.id << " | Name: " << contact.name
                  << " | Phone: " << contact.phone;
        if (!contact.email.empty()) {
            cout << " | Email: " << contact.email;
        }
        cout << '\n';
    }

public:
    bool addContact(const string& name, const string& phone,
                    const string& email) {
        if (idByPhone_.count(phone)) {
            return false;
        }

        Contact contact{nextId_++, name, phone, email};
        contactsById_.emplace(contact.id, contact);
        idByPhone_.emplace(contact.phone, contact.id);
        insertName(namesRoot_, NameKey{contact.name, contact.id});
        recentlyAdded_.push(contact.id);
        if (recentlyAdded_.size() > RECENT_LIMIT) {
            recentlyAdded_.pop();
        }
        undoStack_.push({ActionType::ADD, Contact{}, contact});
        return true;
    }

    bool updateContact(const string& currentPhone, const string& newName,
                       const string& newPhone, const string& newEmail) {
        auto phoneEntry = idByPhone_.find(currentPhone);
        if (phoneEntry == idByPhone_.end()) {
            return false;
        }
        if (newPhone != currentPhone && idByPhone_.count(newPhone)) {
            return false;
        }

        Contact& contact = contactsById_.at(phoneEntry->second);
        const Contact previous = contact;
        eraseName(namesRoot_, NameKey{contact.name, contact.id});
        if (newPhone != currentPhone) {
            idByPhone_.erase(phoneEntry);
            idByPhone_.emplace(newPhone, contact.id);
        }
        contact.name = newName;
        contact.phone = newPhone;
        contact.email = newEmail;
        insertName(namesRoot_, NameKey{contact.name, contact.id});
        undoStack_.push({ActionType::UPDATE, previous, contact});
        return true;
    }

    bool deleteContact(const string& phone) {
        auto phoneEntry = idByPhone_.find(phone);
        if (phoneEntry == idByPhone_.end()) {
            return false;
        }
        const int id = phoneEntry->second;
        const Contact& contact = contactsById_.at(id);
        const Contact deletedContact = contact;
        eraseName(namesRoot_, NameKey{contact.name, contact.id});
        idByPhone_.erase(phoneEntry);
        contactsById_.erase(id);
        undoStack_.push({ActionType::DELETE, deletedContact, Contact{}});
        return true;
    }

    bool undoLastAction() {
        if (undoStack_.empty()) {
            return false;
        }

        const UndoAction action = undoStack_.top();
        undoStack_.pop();
        if (action.type == ActionType::ADD) {
            eraseContactById(action.after.id);
        } else if (action.type == ActionType::DELETE) {
            restoreContact(action.before);
        } else {
            replaceContact(action.before);
        }
        return true;
    }

    bool findByPhone(const string& phone, Contact& result) const {
        auto phoneEntry = idByPhone_.find(phone);
        if (phoneEntry == idByPhone_.end()) {
            return false;
        }
        result = contactsById_.at(phoneEntry->second);
        return true;
    }

    vector<Contact> findByName(const string& name) const {
        vector<Contact> result;
        collectByName(namesRoot_.get(), name, result);
        return result;
    }

    void displayAll() const {
        vector<Contact> contacts;
        collectAlphabetically(namesRoot_.get(), contacts);
        if (contacts.empty()) {
            cout << "No contacts saved.\n";
            return;
        }
        for (const Contact& contact : contacts) {
            printContact(contact);
        }
    }

    void displayRecent() const {
        queue<int> recentCopy = recentlyAdded_;
        bool printedAny = false;
        while (!recentCopy.empty()) {
            auto found = contactsById_.find(recentCopy.front());
            if (found != contactsById_.end()) {
                printContact(found->second);
                printedAny = true;
            }
            recentCopy.pop();
        }
        if (!printedAny) {
            cout << "No recent contacts.\n";
        }
    }
};

static string readLine(const string& prompt) {
    cout << prompt;
    string value;
    getline(cin, value);
    return value;
}

static void showMatches(const vector<Contact>& contacts) {
    if (contacts.empty()) {
        cout << "No matching contacts found.\n";
        return;
    }
    for (const Contact& contact : contacts) {
        cout << "ID: " << contact.id << " | Name: " << contact.name
                  << " | Phone: " << contact.phone;
        if (!contact.email.empty()) {
            cout << " | Email: " << contact.email;
        }
        cout << '\n';
    }
}

int main() {
    ContactManager manager;

    while (true) {
        cout << "\nPhone Contact Manager\n"
                  << "1. Add contact\n"
                  << "2. View all contacts (alphabetical)\n"
                  << "3. Search by phone\n"
                  << "4. Search by exact name\n"
                  << "5. Update contact\n"
                  << "6. Delete contact\n"
                  << "7. View recently added\n"
                  << "8. Undo last action\n"
                  << "0. Exit\n";
        const string choice = readLine("Choose an option: ");

        if (choice == "0") {
            cout << "Goodbye.\n";
            break;
        } else if (choice == "1") {
            const string name = readLine("Name: ");
            const string phone = readLine("Phone: ");
            const string email = readLine("Email (optional): ");
            if (name.empty() || phone.empty()) {
                cout << "Name and phone are required.\n";
            } else if (manager.addContact(name, phone, email)) {
                cout << "Contact added.\n";
            } else {
                cout << "That phone number is already in use.\n";
            }
        } else if (choice == "2") {
            manager.displayAll();
        } else if (choice == "3") {
            Contact contact;
            if (manager.findByPhone(readLine("Phone: "), contact)) {
                showMatches({contact});
            } else {
                cout << "No matching contact found.\n";
            }
        } else if (choice == "4") {
            showMatches(manager.findByName(readLine("Exact name: ")));
        } else if (choice == "5") {
            const string currentPhone = readLine("Current phone number: ");
            Contact existing;
            if (!manager.findByPhone(currentPhone, existing)) {
                cout << "No matching contact found.\n";
                continue;
            }
            const string name = readLine("New name: ");
            const string phone = readLine("New phone: ");
            const string email = readLine("New email (optional): ");
            if (name.empty() || phone.empty()) {
                cout << "Name and phone are required.\n";
            } else if (manager.updateContact(currentPhone, name, phone, email)) {
                cout << "Contact updated.\n";
            } else {
                cout << "That new phone number is already in use.\n";
            }
        } else if (choice == "6") {
            if (manager.deleteContact(readLine("Phone number to delete: "))) {
                cout << "Contact deleted.\n";
            } else {
                cout << "No matching contact found.\n";
            }
        } else if (choice == "7") {
            manager.displayRecent();
        } else if (choice == "8") {
            if (manager.undoLastAction()) {
                cout << "Last action undone.\n";
            } else {
                cout << "There is no action to undo.\n";
            }
        } else {
            cout << "Please choose a listed option.\n";
        }
    }
    return 0;
}
