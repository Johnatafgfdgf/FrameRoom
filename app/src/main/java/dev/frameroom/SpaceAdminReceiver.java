package dev.frameroom;

import android.app.admin.DeviceAdminReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public final class SpaceAdminReceiver extends DeviceAdminReceiver {
    @Override public void onProfileProvisioningComplete(Context context, Intent intent) {
        try {
            SpaceProfile.configure(context);
        } catch (SecurityException error) {
            Log.w("FrameRoomSpace", "O sistema impediu a configuração do perfil", error);
        }
    }
}
