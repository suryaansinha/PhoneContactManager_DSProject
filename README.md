# Phone Contact Manager

A basic menu-driven phone contact manager written in C++17. Contacts are kept in memory while the program runs.

## Features

- Add contacts with a name, unique phone number, and optional email
- Display all contacts alphabetically by name
- Search by phone number or case-insensitive name
- Update a contact by phone number
- Delete a contact by name; choose a contact ID if names are duplicated
- Display the five most recently added contacts
- Undo the most recent add, update, or delete

## Data structures

- `unordered_map<int, Contact>` stores the contact records by stable ID.
- `unordered_map<string, int>` indexes unique phone numbers for average O(1) lookup.
- A binary search tree stores `(name, ID)` keys for alphabetical display and exact-name search. The ID allows contacts with the same name.
- `queue<int>` keeps the five most recently added contact IDs. Deleted contacts are skipped when showing this list.
- `stack<UndoAction>` stores the previous state for add, update, and delete operations. Undo processes the newest change first (LIFO).

The BST is implemented with `unique_ptr` nodes so its memory is released automatically. Its operations are O(log n) on average and O(n) in the worst case because this basic implementation is not self-balancing.

## Build and run

```sh
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o contact_manager
./contact_manager
```

Undo is available for changes made during the current run. Contacts and undo history are not saved after the program exits. File persistence can be added as a later enhancement.

## Sample scenarios

1. **Case-insensitive search:** Add a contact named `Alex Kim`, then search for `alex kim` or `ALEX KIM`. The contact is found either way.
2. **Delete by name:** Choose **Delete contact by name** and enter `alex kim`. The matching contact is shown and deleted. If several contacts have that name, select the ID of the one to delete, or enter `0` to cancel.
3. **Undo a deletion:** Delete a contact, choose **Undo last action**, then view all contacts. The deleted contact is restored.
4. **Undo an update:** Change a contact's name or phone number, then choose **Undo last action**. Its previous details are restored.
