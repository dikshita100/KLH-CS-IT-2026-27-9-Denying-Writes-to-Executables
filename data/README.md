#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define LOG_FILE "protection.log"
#define MAX_PATH 500

/* GUI widgets */
GtkWidget *window;
GtkWidget *path_entry;
GtkWidget *output_view;

/* ---------------------------------------------------------
   ORIGINAL PROJECT LOGIC
   --------------------------------------------------------- */

/* Function to write messages into log file */
void writeLog(const char *message, const char *filePath)
{
    FILE *log = fopen(LOG_FILE, "a");

    if (log == NULL)
    {
        return;
    }

    time_t now = time(NULL);
    char *timeString = ctime(&now);

    if (timeString != NULL)
        timeString[strcspn(timeString, "\n")] = '\0';

    fprintf(log, "[%s] %s : %s\n",
            timeString, message, filePath);

    fclose(log);
}

/* Check whether file is executable */
int isExecutable(const char *filePath)
{
    struct stat fileInfo;

    if (stat(filePath, &fileInfo) != 0)
    {
        return 0;
    }

    if (!S_ISREG(fileInfo.st_mode))
    {
        return 0;
    }

    if (fileInfo.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
    {
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------
   GUI OUTPUT FUNCTIONS
   --------------------------------------------------------- */

void append_output(const char *text)
{
    GtkTextBuffer *buffer;
    GtkTextIter end;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_view));

    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
}

void clear_output(void)
{
    GtkTextBuffer *buffer;

    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(output_view));
    gtk_text_buffer_set_text(buffer, "", -1);
}

const char *get_file_path(void)
{
    return gtk_entry_get_text(GTK_ENTRY(path_entry));
}

/* Display file permissions in the GUI */
void displayPermissions(const char *filePath)
{
    struct stat fileInfo;
    char output[2000];

    if (stat(filePath, &fileInfo) != 0)
    {
        snprintf(output, sizeof(output),
                 "File error: %s\n\n", strerror(errno));
        append_output(output);
        return;
    }

    char permissions[10];

    permissions[0] = (fileInfo.st_mode & S_IRUSR) ? 'r' : '-';
    permissions[1] = (fileInfo.st_mode & S_IWUSR) ? 'w' : '-';
    permissions[2] = (fileInfo.st_mode & S_IXUSR) ? 'x' : '-';

    permissions[3] = (fileInfo.st_mode & S_IRGRP) ? 'r' : '-';
    permissions[4] = (fileInfo.st_mode & S_IWGRP) ? 'w' : '-';
    permissions[5] = (fileInfo.st_mode & S_IXGRP) ? 'x' : '-';

    permissions[6] = (fileInfo.st_mode & S_IROTH) ? 'r' : '-';
    permissions[7] = (fileInfo.st_mode & S_IWOTH) ? 'w' : '-';
    permissions[8] = (fileInfo.st_mode & S_IXOTH) ? 'x' : '-';
    permissions[9] = '\0';

    snprintf(output, sizeof(output),
             "----------------------------------------\n"
             "FILE PERMISSION DETAILS\n"
             "----------------------------------------\n"
             "File: %s\n\n"
             "Permissions: %s\n\n"
             "Owner Write Permission : %s\n"
             "Group Write Permission : %s\n"
             "Others Write Permission: %s\n"
             "----------------------------------------\n\n",
             filePath,
             permissions,
             (fileInfo.st_mode & S_IWUSR) ? "YES" : "NO",
             (fileInfo.st_mode & S_IWGRP) ? "YES" : "NO",
             (fileInfo.st_mode & S_IWOTH) ? "YES" : "NO");

    append_output(output);
}

/* ---------------------------------------------------------
   PROJECT OPERATIONS
   --------------------------------------------------------- */

/* Identify executable */
void identifyExecutable(void)
{
    const char *filePath = get_file_path();
    char output[1000];

    if (filePath == NULL || strlen(filePath) == 0)
    {
        append_output("Please enter an executable file path.\n\n");
        return;
    }

    if (access(filePath, F_OK) != 0)
    {
        snprintf(output, sizeof(output),
                 "File error: %s\n\n", strerror(errno));
        append_output(output);
        return;
    }

    if (isExecutable(filePath))
    {
        snprintf(output, sizeof(output),
                 "========================================\n"
                 "EXECUTABLE IDENTIFICATION\n"
                 "========================================\n"
                 "Executable identified successfully.\n\n"
                 "File: %s\n"
                 "Status: EXECUTABLE\n"
                 "========================================\n\n",
                 filePath);

        append_output(output);

        writeLog("Executable identified", filePath);
    }
    else
    {
        append_output(
            "========================================\n"
            "EXECUTABLE IDENTIFICATION\n"
            "========================================\n"
            "The specified file is NOT executable.\n"
            "========================================\n\n");
    }
}

/* Check permissions */
void checkPermissions(void)
{
    const char *filePath = get_file_path();
    char output[500];

    if (filePath == NULL || strlen(filePath) == 0)
    {
        append_output("Please enter an executable file path.\n\n");
        return;
    }

    if (access(filePath, F_OK) != 0)
    {
        snprintf(output, sizeof(output),
                 "File error: %s\n\n", strerror(errno));
        append_output(output);
        return;
    }

    if (!isExecutable(filePath))
    {
        append_output("This is not an executable file.\n\n");
        return;
    }

    displayPermissions(filePath);
}

/* Protect executable by removing write permissions */
void protectExecutable(void)
{
    const char *filePath = get_file_path();
    struct stat fileInfo;
    char output[1000];

    if (filePath == NULL || strlen(filePath) == 0)
    {
        append_output("Please enter an executable file path.\n\n");
        return;
    }

    if (stat(filePath, &fileInfo) != 0)
    {
        snprintf(output, sizeof(output),
                 "File error: %s\n\n", strerror(errno));
        append_output(output);
        return;
    }

    if (!isExecutable(filePath))
    {
        append_output("This is not an executable file.\n\n");
        return;
    }

    /*
     * Remove write permissions for:
     * Owner
     * Group
     * Others
     */
    mode_t newMode = fileInfo.st_mode;

    newMode &= ~(S_IWUSR | S_IWGRP | S_IWOTH);

    if (chmod(filePath, newMode) != 0)
    {
        snprintf(output, sizeof(output),
                 "Protection failed: %s\n\n", strerror(errno));
        append_output(output);

        writeLog("Protection failed", filePath);
        return;
    }

    snprintf(output, sizeof(output),
             "========================================\n"
             "EXECUTABLE PROTECTION\n"
             "========================================\n"
             "Executable protected successfully!\n\n"
             "Write permissions have been removed.\n"
             "The executable permission remains unchanged.\n"
             "========================================\n\n");

    append_output(output);

    writeLog("Executable protected - write permissions removed",
             filePath);
}

/* Attempt to write to executable */
void attemptWrite(void)
{
    const char *filePath = get_file_path();
    int fd;
    char output[1200];

    if (filePath == NULL || strlen(filePath) == 0)
    {
        append_output("Please enter an executable file path.\n\n");
        return;
    }

    if (access(filePath, F_OK) != 0)
    {
        snprintf(output, sizeof(output),
                 "File error: %s\n\n", strerror(errno));
        append_output(output);
        return;
    }

    if (!isExecutable(filePath))
    {
        append_output("This is not an executable file.\n\n");
        return;
    }

    append_output(
        "========================================\n"
        "WRITE ACCESS TEST\n"
        "========================================\n"
        "Attempting to write to executable...\n\n");

    /*
     * Try opening file for writing.
     */
    fd = open(filePath, O_WRONLY | O_APPEND);

    if (fd == -1)
    {
        snprintf(output, sizeof(output),
                 "WRITE DENIED!\n"
                 "Reason: %s\n"
                 "Status: BLOCKED\n"
                 "========================================\n\n",
                 strerror(errno));

        append_output(output);

        writeLog("Unauthorized write attempt BLOCKED",
                 filePath);

        return;
    }

    append_output(
        "Write access was allowed.\n"
        "Status: ALLOWED\n"
        "========================================\n\n");

    close(fd);

    writeLog("Write attempt allowed", filePath);
}

/* View protection log */
void viewLog(void)
{
    FILE *log = fopen(LOG_FILE, "r");
    char line[1000];

    if (log == NULL)
    {
        append_output(
            "========================================\n"
            "PROTECTION LOG\n"
            "========================================\n"
            "No protection log found.\n"
            "========================================\n\n");
        return;
    }

    append_output(
        "========================================\n"
        "PROTECTION LOG\n"
        "========================================\n");

    while (fgets(line, sizeof(line), log) != NULL)
    {
        append_output(line);
    }

    append_output(
        "========================================\n\n");

    fclose(log);
}

/* ---------------------------------------------------------
   GTK BUTTON CALLBACKS
   --------------------------------------------------------- */

void on_identify_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    clear_output();
    identifyExecutable();
}

void on_permissions_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    clear_output();
    checkPermissions();
}

void on_protect_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    clear_output();
    protectExecutable();
}

void on_write_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    clear_output();
    attemptWrite();
}

void on_log_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    clear_output();
    viewLog();
}

void on_clear_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    clear_output();
}

void on_exit_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;

    gtk_main_quit();
}

/* ---------------------------------------------------------
   GUI CREATION
   --------------------------------------------------------- */

void create_gui(void)
{
    GtkWidget *main_box;
    GtkWidget *title_label;
    GtkWidget *path_label;
    GtkWidget *button_grid;
    GtkWidget *scroll_window;

    /* Main window */
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    gtk_window_set_title(
        GTK_WINDOW(window),
        "Executable Protection System");

    gtk_window_set_default_size(
        GTK_WINDOW(window), 900, 650);

    gtk_window_set_position(
        GTK_WINDOW(window),
        GTK_WIN_POS_CENTER);

    g_signal_connect(
        window,
        "destroy",
        G_CALLBACK(on_exit_clicked),
        NULL);

    /* Main vertical container */
    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);

    gtk_container_set_border_width(
        GTK_CONTAINER(main_box), 20);

    gtk_container_add(
        GTK_CONTAINER(window), main_box);

    /* Title */
    title_label = gtk_label_new(
        "EXECUTABLE PROTECTION SYSTEM");

    gtk_widget_set_halign(
        title_label, GTK_ALIGN_CENTER);

    gtk_box_pack_start(
        GTK_BOX(main_box),
        title_label,
        FALSE,
        FALSE,
        5);

    /* Description */
    GtkWidget *description = gtk_label_new(
        "Denying Writes to Executables - Linux OS Project");

    gtk_widget_set_halign(
        description, GTK_ALIGN_CENTER);

    gtk_box_pack_start(
        GTK_BOX(main_box),
        description,
        FALSE,
        FALSE,
        2);

    /* File path label */
    path_label = gtk_label_new(
        "Enter executable file path:");

    gtk_widget_set_halign(
        path_label, GTK_ALIGN_START);

    gtk_box_pack_start(
        GTK_BOX(main_box),
        path_label,
        FALSE,
        FALSE,
        5);

    /* Path entry */
    path_entry = gtk_entry_new();

    gtk_entry_set_placeholder_text(
        GTK_ENTRY(path_entry),
        "/home/username/project/logger");

    gtk_box_pack_start(
        GTK_BOX(main_box),
        path_entry,
        FALSE,
        FALSE,
        5);

    /* Button grid */
    button_grid = gtk_grid_new();

    gtk_grid_set_row_spacing(
        GTK_GRID(button_grid), 8);

    gtk_grid_set_column_spacing(
        GTK_GRID(button_grid), 8);

    gtk_box_pack_start(
        GTK_BOX(main_box),
        button_grid,
        FALSE,
        FALSE,
        5);

    GtkWidget *identify_button =
        gtk_button_new_with_label(
            "1. Identify Executable");

    GtkWidget *permissions_button =
        gtk_button_new_with_label(
            "2. Check Permissions");

    GtkWidget *protect_button =
        gtk_button_new_with_label(
            "3. Protect Executable");

    GtkWidget *write_button =
        gtk_button_new_with_label(
            "4. Attempt Write");

    GtkWidget *log_button =
        gtk_button_new_with_label(
            "5. View Protection Log");

    GtkWidget *clear_button =
        gtk_button_new_with_label("Clear Output");

    GtkWidget *exit_button =
        gtk_button_new_with_label("6. Exit");

    gtk_grid_attach(GTK_GRID(button_grid),
                    identify_button, 0, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(button_grid),
                    permissions_button, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(button_grid),
                    protect_button, 2, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(button_grid),
                    write_button, 0, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(button_grid),
                    log_button, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(button_grid),
                    clear_button, 2, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(button_grid),
                    exit_button, 3, 1, 1, 1);

    /* Connect buttons */
    g_signal_connect(
        identify_button, "clicked",
        G_CALLBACK(on_identify_clicked), NULL);

    g_signal_connect(
        permissions_button, "clicked",
        G_CALLBACK(on_permissions_clicked), NULL);

    g_signal_connect(
        protect_button, "clicked",
        G_CALLBACK(on_protect_clicked), NULL);

    g_signal_connect(
        write_button, "clicked",
        G_CALLBACK(on_write_clicked), NULL);

    g_signal_connect(
        log_button, "clicked",
        G_CALLBACK(on_log_clicked), NULL);

    g_signal_connect(
        clear_button, "clicked",
        G_CALLBACK(on_clear_clicked), NULL);

    g_signal_connect(
        exit_button, "clicked",
        G_CALLBACK(on_exit_clicked), NULL);

    /* Output area */
    GtkWidget *output_label =
        gtk_label_new("Project Output:");

    gtk_widget_set_halign(
        output_label, GTK_ALIGN_START);

    gtk_box_pack_start(
        GTK_BOX(main_box),
        output_label,
        FALSE,
        FALSE,
        2);

    output_view = gtk_text_view_new();

    gtk_text_view_set_editable(
        GTK_TEXT_VIEW(output_view), FALSE);

    gtk_text_view_set_cursor_visible(
        GTK_TEXT_VIEW(output_view), FALSE);

    gtk_text_view_set_wrap_mode(
        GTK_TEXT_VIEW(output_view),
        GTK_WRAP_WORD_CHAR);

    /* Scrollable output */
    scroll_window = gtk_scrolled_window_new(NULL, NULL);

    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scroll_window),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC);

    gtk_container_add(
        GTK_CONTAINER(scroll_window),
        output_view);

    gtk_box_pack_start(
        GTK_BOX(main_box),
        scroll_window,
        TRUE,
        TRUE,
        5);

    /* Initial GUI message */
    append_output(
        "========================================\n"
        "EXECUTABLE PROTECTION SYSTEM\n"
        "========================================\n"
        "GUI initialized successfully.\n\n"
        "Enter the executable file path above,\n"
        "then select an operation.\n\n"
        "Project functions:\n"
        "1. Identify Executable\n"
        "2. Check Permissions\n"
        "3. Protect Executable\n"
        "4. Attempt Write\n"
        "5. View Protection Log\n"
        "6. Exit\n"
        "========================================\n");

    gtk_widget_show_all(window);
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    create_gui();

    gtk_main();

    return 0;
}
