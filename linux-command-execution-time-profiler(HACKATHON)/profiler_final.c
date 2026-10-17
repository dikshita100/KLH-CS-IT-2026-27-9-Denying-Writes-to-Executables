#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/ptrace.h>
#include <sys/user.h>

#define MAX_SYSCALLS 512
#define MAX_SAMPLES 300

typedef struct {
    long count;
    char name[32];
} SyscallStat;

typedef struct {
    char command[256];
    double real_time_ms;
    double user_time_ms;
    double sys_time_ms;
    long max_rss_kb;
    long total_syscalls;
    int exit_status;
    SyscallStat syscall_table[MAX_SYSCALLS];
    long memory_samples[MAX_SAMPLES];
    int sample_count;
} ProfileResult;

typedef struct {
    GtkWidget *entry;
    GtkWidget *output_text;
    GtkWidget *status_label;
    GtkWidget *drawing_area;
    GtkWidget *mem_drawing_area;
    GtkWidget *run_button;
    GtkListStore *syscall_list_store;
    ProfileResult last_result;
    int hovered_bar;
} AppWidgets;

static const char* get_syscall_name(int sys_num) {
    switch(sys_num) {
        case 0: return "read";
        case 1: return "write";
        case 2: return "open";
        case 3: return "close";
        case 8: return "lseek";
        case 9: return "mmap";
        case 10: return "mprotect";
        case 11: return "munmap";
        case 12: return "brk";
        case 59: return "execve";
        case 257: return "openat";
        default: return "other_syscall";
    }
}

static double timespec_to_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

// Chart Visualization: Properly Scales Relative Bar Lengths
static gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    ProfileResult *res = &app->last_result;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.15);
    cairo_paint(cr);

    if (res->real_time_ms <= 0 && res->user_time_ms <= 0 && res->sys_time_ms <= 0) {
        cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 13.0);
        cairo_move_to(cr, 20, height / 2);
        cairo_show_text(cr, "Execute a command to display profile execution metrics...");
        return FALSE;
    }

    double max_val = res->real_time_ms;
    if (res->user_time_ms > max_val) max_val = res->user_time_ms;
    if (res->sys_time_ms > max_val) max_val = res->sys_time_ms;
    if (max_val <= 0) max_val = 1.0;

    double max_bar_width = width - 220.0;
    double values[3] = {res->real_time_ms, res->user_time_ms, res->sys_time_ms};
    const char *labels[3] = {"Real Time", "User CPU", "System CPU"};

    for (int i = 0; i < 3; i++) {
        double y = 25.0 + i * 40.0;
        double bar_w = (values[i] / max_val) * max_bar_width;
        if (bar_w < 3.0 && values[i] > 0) bar_w = 3.0;

        if (app->hovered_bar == i) {
            cairo_set_source_rgb(cr, 0.95, 0.75, 0.2);
        } else {
            if (i == 0) cairo_set_source_rgb(cr, 0.2, 0.6, 0.86);
            else if (i == 1) cairo_set_source_rgb(cr, 0.2, 0.78, 0.48);
            else cairo_set_source_rgb(cr, 0.9, 0.35, 0.3);
        }

        cairo_rectangle(cr, 140, y, bar_w, 24.0);
        cairo_fill(cr);

        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_set_font_size(cr, 12.0);
        cairo_move_to(cr, 15, y + 17);
        cairo_show_text(cr, labels[i]);

        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f ms", values[i]);
        cairo_move_to(cr, 145 + bar_w, y + 17);
        cairo_show_text(cr, buf);
    }

    return FALSE;
}

// Live Memory Graph Drawing Area
static gboolean on_draw_mem_chart(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    ProfileResult *res = &app->last_result;

    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    cairo_set_source_rgb(cr, 0.1, 0.1, 0.12);
    cairo_paint(cr);

    if (res->sample_count < 2) {
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_move_to(cr, 20, height / 2);
        cairo_show_text(cr, "Memory timeline curve will appear here after profiling...");
        return FALSE;
    }

    cairo_set_source_rgb(cr, 0.2, 0.8, 0.4);
    cairo_set_line_width(cr, 2.0);

    long max_mem = 1;
    for (int i = 0; i < res->sample_count; i++) {
        if (res->memory_samples[i] > max_mem) max_mem = res->memory_samples[i];
    }

    double step_x = (double)width / (res->sample_count - 1);
    for (int i = 0; i < res->sample_count; i++) {
        double x = i * step_x;
        double y = height - ((double)res->memory_samples[i] / max_mem * (height - 30)) - 15;
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);

    return FALSE;
}

static gboolean on_mouse_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    int prev_hover = app->hovered_bar;
    app->hovered_bar = -1;

    for (int i = 0; i < 3; i++) {
        double y_start = 25.0 + i * 40.0;
        if (event->y >= y_start && event->y <= y_start + 24.0) {
            app->hovered_bar = i;
            break;
        }
    }

    if (prev_hover != app->hovered_bar) {
        gtk_widget_queue_draw(widget);
    }
    return FALSE;
}

static gboolean update_ui_callback(gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    ProfileResult *res = &app->last_result;

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->output_text));
    char summary[512];

    snprintf(summary, sizeof(summary),
             "Command         : %s\n"
             "Exit Code       : %d\n"
             "Real Time       : %.3f ms\n"
             "User Time       : %.3f ms\n"
             "System Time     : %.3f ms\n"
             "Peak RSS Memory : %ld KB\n"
             "Total Syscalls  : %ld\n",
             res->command, res->exit_status, res->real_time_ms,
             res->user_time_ms, res->sys_time_ms, res->max_rss_kb, res->total_syscalls);

    gtk_text_buffer_set_text(buf, summary, -1);

    // Update Syscall Frequency Breakdown List
    gtk_list_store_clear(app->syscall_list_store);
    GtkTreeIter iter;
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        if (res->syscall_table[i].count > 0) {
            gtk_list_store_append(app->syscall_list_store, &iter);
            gtk_list_store_set(app->syscall_list_store, &iter,
                               0, res->syscall_table[i].name,
                               1, res->syscall_table[i].count,
                               -1);
        }
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), "Execution Profiling complete!");
    gtk_widget_set_sensitive(app->run_button, TRUE);
    gtk_widget_queue_draw(app->drawing_area);
    gtk_widget_queue_draw(app->mem_drawing_area);

    return G_SOURCE_REMOVE;
}

// Optimized Worker Thread
static void *profiler_worker_thread(void *arg) {
    AppWidgets *app = (AppWidgets *)arg;
    ProfileResult *res = &app->last_result;

    struct timespec start_wall, end_wall;
    struct rusage usage;
    memset(res->syscall_table, 0, sizeof(res->syscall_table));
    res->sample_count = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_wall);

    pid_t pid = fork();
    if (pid == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        execl("/bin/sh", "sh", "-c", res->command, (char *)NULL);
        exit(127);
    } else if (pid > 0) {
        int status;
        long total_sys = 0;

        waitpid(pid, &status, 0);
        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);

        while (1) {
            ptrace(PTRACE_SYSCALL, pid, 0, 0);
            waitpid(pid, &status, 0);

            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                res->exit_status = WEXITSTATUS(status);
                break;
            }

            if (WIFSTOPPED(status) && (WSTOPSIG(status) & 0x80)) {
                struct user_regs_struct regs;
                if (ptrace(PTRACE_GETREGS, pid, 0, &regs) == 0) {
                    long sys_num = regs.orig_rax;
                    if (sys_num >= 0 && sys_num < MAX_SYSCALLS) {
                        res->syscall_table[sys_num].count++;
                        snprintf(res->syscall_table[sys_num].name, 32, "%s", get_syscall_name(sys_num));
                    }
                }
                total_sys++;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &end_wall);
        getrusage(RUSAGE_CHILDREN, &usage);

        res->real_time_ms = timespec_to_ms(start_wall, end_wall);
        res->user_time_ms = (usage.ru_utime.tv_sec * 1000.0) + (usage.ru_utime.tv_usec / 1000.0);
        res->sys_time_ms  = (usage.ru_stime.tv_sec * 1000.0) + (usage.ru_stime.tv_usec / 1000.0);
        res->max_rss_kb   = usage.ru_maxrss;
        res->total_syscalls = total_sys / 2;
    }

    g_idle_add(update_ui_callback, app);
    return NULL;
}

static void on_run_clicked(GtkWidget *widget, gpointer user_data) {
    AppWidgets *app = (AppWidgets *)user_data;
    const char *cmd = gtk_entry_get_text(GTK_ENTRY(app->entry)); // Fixed app->entry pointer access

    if (strlen(cmd) == 0) return;

    strncpy(app->last_result.command, cmd, sizeof(app->last_result.command));
    gtk_label_set_text(GTK_LABEL(app->status_label), "Profiling command...");
    gtk_widget_set_sensitive(app->run_button, FALSE);

    pthread_t thread;
    pthread_create(&thread, NULL, profiler_worker_thread, app);
    pthread_detach(thread);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppWidgets app = {0};
    app.hovered_bar = -1;

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Linux Execution Time Profiler");
    gtk_window_set_default_size(GTK_WINDOW(window), 700, 520);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // Command Entry Line
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new("Target Command:");
    app.entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app.entry), "ls -la");
    app.run_button = gtk_button_new_with_label("Start Profiling");

    g_signal_connect(app.run_button, "clicked", G_CALLBACK(on_run_clicked), &app);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), app.entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), app.run_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    app.status_label = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(vbox), app.status_label, FALSE, FALSE, 0);

    // Multi-Tab Notebook Layout
    GtkWidget *notebook = gtk_notebook_new();

    // Tab 1: Execution Profile Metrics & Visualizer
    GtkWidget *tab1_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    app.output_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app.output_text), FALSE);
    GtkWidget *scrolled1 = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scrolled1, -1, 130);
    gtk_container_add(GTK_CONTAINER(scrolled1), app.output_text);
    gtk_box_pack_start(GTK_BOX(tab1_vbox), scrolled1, FALSE, FALSE, 0);

    app.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(app.drawing_area, -1, 160);
    gtk_widget_add_events(app.drawing_area, GDK_POINTER_MOTION_MASK);
    g_signal_connect(G_OBJECT(app.drawing_area), "draw", G_CALLBACK(on_draw_chart), &app);
    g_signal_connect(G_OBJECT(app.drawing_area), "motion-notify-event", G_CALLBACK(on_mouse_motion), &app);
    gtk_box_pack_start(GTK_BOX(tab1_vbox), app.drawing_area, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab1_vbox, gtk_label_new("Execution Profile"));

    // Tab 2: System Call Frequency Inspector
    app.syscall_list_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_LONG);
    GtkWidget *tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.syscall_list_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col1 = gtk_tree_view_column_new_with_attributes("Syscall Name", renderer, "text", 0, NULL);
    GtkTreeViewColumn *col2 = gtk_tree_view_column_new_with_attributes("Call Count", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col1);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), col2);

    GtkWidget *scrolled2 = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled2), tree_view);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled2, gtk_label_new("Syscall Breakdown"));

    // Tab 3: Live Memory Usage Graph
    app.mem_drawing_area = gtk_drawing_area_new();
    g_signal_connect(G_OBJECT(app.mem_drawing_area), "draw", G_CALLBACK(on_draw_mem_chart), &app);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), app.mem_drawing_area, gtk_label_new("Live Memory Graph"));

    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
