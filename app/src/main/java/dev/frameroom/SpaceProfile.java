package dev.frameroom;

import android.app.Activity;
import android.app.admin.DevicePolicyManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.UserManager;
import android.util.Log;

final class SpaceProfile {
    static final String PACKAGE = "clonePackage";
    static final String APP_LABEL = "cloneLabel";

    private SpaceProfile() {}

    static ComponentName admin(Context context) { return new ComponentName(context, SpaceAdminReceiver.class); }

    static boolean isOwned(Context context) {
        DevicePolicyManager policy = context.getSystemService(DevicePolicyManager.class);
        UserManager users = context.getSystemService(UserManager.class);
        return users != null && users.isManagedProfile() && policy != null &&
            policy.isProfileOwnerApp(context.getPackageName());
    }

    static Intent provision(Context context) {
        DevicePolicyManager policy = context.getSystemService(DevicePolicyManager.class);
        if (policy == null || !policy.isProvisioningAllowed(DevicePolicyManager.ACTION_PROVISION_MANAGED_PROFILE))
            return null;
        Intent intent = new Intent(DevicePolicyManager.ACTION_PROVISION_MANAGED_PROFILE);
        intent.putExtra(DevicePolicyManager.EXTRA_PROVISIONING_DEVICE_ADMIN_COMPONENT_NAME, admin(context));
        return intent;
    }

    static boolean configure(Context context) {
        if (!isOwned(context)) return false;
        DevicePolicyManager policy = context.getSystemService(DevicePolicyManager.class);
        ComponentName component = admin(context);
        context.getPackageManager().setComponentEnabledSetting(new ComponentName(context, SpaceBridgeActivity.class),
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED, PackageManager.DONT_KILL_APP);
        var settings = context.getSharedPreferences("space_profile", Context.MODE_PRIVATE);
        if (!settings.getBoolean("configured", false)) {
            IntentFilter inbound = new IntentFilter(action(context, "OPEN"));
            inbound.addAction(action(context, "INSTALL"));
            inbound.addCategory(Intent.CATEGORY_DEFAULT);
            policy.addCrossProfileIntentFilter(component, inbound, DevicePolicyManager.FLAG_PARENT_CAN_ACCESS_MANAGED);
            policy.setProfileName(component, "FrameRoom");
            policy.setProfileEnabled(component);
            settings.edit().putBoolean("configured", true).apply();
        }
        if (!settings.getBoolean("installerPolicyConfigured", false)) {
            try {
                // Set the policy of this app's own work profile. The system still handles per-source consent.
                policy.clearUserRestriction(component, UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES);
                settings.edit().putBoolean("installerPolicyConfigured", true).apply();
            } catch (SecurityException error) {
                Log.w("FrameRoomSpace", "O Android controla a política de instalação deste perfil", error);
            }
        }
        return true;
    }

    static String action(Context context, String suffix) {
        return context.getPackageName() + ".space." + suffix;
    }

    static Intent openIntent(Context context) {
        return new Intent(action(context, "OPEN")).addCategory(Intent.CATEGORY_DEFAULT);
    }

    static Intent cloneIntent(Context context, String pkg, String label) {
        return new Intent(action(context, "INSTALL")).addCategory(Intent.CATEGORY_DEFAULT)
            .putExtra(PACKAGE, pkg).putExtra(APP_LABEL, label);
    }

    private static ComponentName systemForwarder(Context context, Intent intent) {
        PackageManager packages = context.getPackageManager();
        for (ResolveInfo result : packages.queryIntentActivities(intent, PackageManager.MATCH_DEFAULT_ONLY)) {
            if (result.activityInfo != null && "android".equals(result.activityInfo.packageName) &&
                result.activityInfo.name.endsWith("ForwardIntentToManagedProfile")) {
                return new ComponentName(result.activityInfo.packageName, result.activityInfo.name);
            }
        }
        return null;
    }

    static boolean canOpen(Context context) {
        return systemForwarder(context, openIntent(context)) != null;
    }

    static void forward(Activity activity, Intent intent) {
        ComponentName forwarder = systemForwarder(activity, intent);
        if (forwarder == null) {
            throw new IllegalStateException("O perfil FrameRoom ainda não está disponível. Confira se ele está ativo no Android.");
        }
        activity.startActivity(intent.setComponent(forwarder));
    }
}
