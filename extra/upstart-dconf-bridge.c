#include <dconf.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>

void dconf_changed (DConfClient *client, const gchar *prefix,
                const gchar * const *changes, const gchar *tag,
                DBusGProxy *upstart) {

    GVariant* value;
    gchar* value_str = NULL;
    gchar* path = NULL;
    gchar* env_key = NULL;
    gchar* env_value = NULL;
    char **env;
    GError *error = NULL;
    int i = 0;

    while (1) {
        if (changes[i] == NULL) {
            break;
        }

        path = g_strconcat(prefix, path, NULL);

        value = dconf_client_read(client, path);
        value_str = g_variant_print(value, FALSE);

        printf("%s => %s\n", path, value_str);

        env_key = g_strconcat("DCONF_KEY=", path, NULL);
        env_value = g_strconcat("DCONF_VALUE=", value_str, NULL);

        env = g_new (char *, 2);
        env[0] = env_key;
        env[1] = env_value;
        env[2] = NULL;

        dbus_g_proxy_call(upstart, "EmitEvent", &error,
                        G_TYPE_STRING, "dconf-changed",
                        G_TYPE_STRV, env,
                        G_TYPE_BOOLEAN, FALSE,
                        G_TYPE_INVALID,
                        G_TYPE_INVALID);

        if (error) {
            g_error("D-BUS: %s", error->message);
            g_error_free(error);
        }

        g_free(path);
        g_variant_unref(value);
        g_free(value_str);
        g_free(env_key);
        g_free(env_value);
        g_free(env);

        i += 1;
    }
}


int main() {
    DConfClient* client;
    GMainLoop *mainloop;
    DBusGConnection *dbus;
    DBusGProxy *upstart;
    GError *error = NULL;

    g_type_init();

    client = dconf_client_new();
    mainloop = g_main_loop_new(NULL, FALSE);

    dbus = dbus_g_bus_get(DBUS_BUS_SESSION, &error);
    if (error != NULL) {
        g_error("D-BUS Connection error: %s", error->message);
        g_error_free(error);
    }

    if (!dbus) {
        g_error("D-BUS connection cannot be created");
        return 1;
    }

    upstart = dbus_g_proxy_new_for_name(dbus,
            "com.ubuntu.Upstart",
            "/com/ubuntu/Upstart",
            "com.ubuntu.Upstart0_6");

    if (!upstart) {
        g_error("Cannot connect to upstart");
        return 1;
    }

    g_signal_connect(client, "changed", (GCallback) dconf_changed, upstart);
    dconf_client_watch_sync(client, "/");

    g_main_loop_run(mainloop);

    g_object_unref(client);
    g_main_loop_unref(mainloop);
    return 0;
}
