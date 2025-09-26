## Implementation Plan: Local Print History Manager and UI

### 1. **Design Data Model**

- Define the `PrintHistoryEntry` struct (already done):
    - Fields: id, filename, device_name, print_time, status, duration, user_notes, gcode_path.

### 2. **Database Integration**

- Add SQLite3 as a dependency if not already included.
- Implement `PrintHistoryManager`:
    - Constructor opens/creates SQLite database (e.g., `print_history.db`).
    - On startup, ensures the print_history table exists.
    - Methods to:
        - Add a print job entry.
        - Retrieve all entries, sorted by print_time.
        - Export entries to CSV.
        - Export entries to JSON.

### 3. **UI Widget Development**

- Implement `PrintHistoryWidget` (subclass of QWidget):
    - Accepts a pointer/reference to a `PrintHistoryManager`.
    - Displays entries in a `QTableWidget`.
    - Adds an “Export” button.
    - On export, lets user pick CSV/JSON and writes the file using `PrintHistoryManager`.

### 4. **Integration with Main Application**

- Instantiate `PrintHistoryManager` at application startup.
- Wire print job completion events to call `add_entry`.
- Add `PrintHistoryWidget` to a new tab or section in the main window.
- Ensure the UI widget refreshes after new jobs are added.

### 5. **Testing and Validation**

- Add test print jobs (simulate or use real events).
- Verify entries show in the history table.
- Test CSV and JSON export for correctness.
- Handle database errors gracefully (file permissions, corruption).

### 6. **Documentation**

- Comment all new classes and methods.
- Add a section to the README about the print history feature.
- Document where the database file is stored and export options.

### 7. **Future Extensions (Optional, Not MVP)**

- Add filtering/searching/sorting in the UI.
- Add editing/deleting history entries.
- Support import of CSV/JSON.
- Add settings for database location.

---

## Milestones & Checklist

- [x] Define data model (`PrintHistoryEntry`)
- [x] Implement SQLite-based manager (`PrintHistoryManager`)
- [x] Implement Qt UI widget (`PrintHistoryWidget`)
- [ ] Integrate into main window & print flow
- [ ] Test with real data and edge cases
- [ ] Document usage and storage
- [ ] Polish and PR for review

---

## Example Directory Structure

```
src/print_history/
    PrintHistoryManager.hpp
    PrintHistoryManager.cpp
    PrintHistoryWidget.hpp
    PrintHistoryWidget.cpp
    CMakeLists.txt
```

---

Would you like a step-by-step task breakdown or code snippets for any of these steps?
