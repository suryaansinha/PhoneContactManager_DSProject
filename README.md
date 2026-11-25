# Phone Contact Manager

A basic menu-driven phone contact manager written entirely in C. Contacts are saved in a text file so they remain available after the program exits.

## Features

- Add contacts with a name, unique phone number, and optional email
- Display contacts alphabetically by name
- Search by phone number or case-insensitive exact name
- Update contacts by phone number
- Delete contacts by name; choose a contact ID when names are duplicated
- View the five most recently added contacts during the current session
- Undo the most recent add, update, or delete during the current session
- Automatically load contacts at startup and save after changes and on exit

## Data structures

- A linked list stores the contact records.
- A hash table with separate chaining indexes phone numbers for fast lookup.
- A binary search tree indexes contacts by case-insensitive name and ID for alphabetical display and name search.
- A queue stores up to five recently added contact IDs.
- A stack stores the previous state for add, update, and delete operations so the last change can be undone first (LIFO).

These structures are implemented directly with C structs, pointers, and functions. The binary search tree is not self-balancing, so its operations take O(log n) time on average and O(n) in the worst case.

## Build and run

```sh
cc -std=c11 -Wall -Wextra -pedantic main.c -o contact_manager
./contact_manager
```

The program reads and writes `contacts.txt` in its current working directory. Each record is stored as a tab-separated row containing the contact ID, name, phone number, and email. Tabs are not accepted in entered fields because they are used as file separators. If a row in the file is malformed or has a duplicate ID or phone number, that row is skipped when loading.

Contact records persist between runs. The undo history and recent-contacts queue are session-only and start empty each time the program launches.

## Sample scenarios

1. **Persistence:** Add `Alex Kim` with phone `555-0101`, exit, then run the program again. The contact loads from `contacts.txt`.
2. **Case-insensitive search:** Search for `alex kim` or `ALEX KIM`; both find `Alex Kim`.
3. **Delete by name:** Choose **Delete contact by name** and enter `alex kim`. If multiple contacts match, choose the ID of the contact to delete, or enter `0` to cancel.
4. **Undo a deletion:** Delete a contact, choose **Undo last action**, then view all contacts. The contact is restored and the file is updated.
5. **Undo an update:** Change a contact's name or phone number, then choose **Undo last action**. Its previous details are restored and saved.
