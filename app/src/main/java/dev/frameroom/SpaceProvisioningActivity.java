package dev.frameroom;

import android.app.Activity;
import android.app.admin.DevicePolicyManager;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import java.util.ArrayList;

/** Answers the platform's provisioning requests for our work profile only. */
public final class SpaceProvisioningActivity extends Activity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Intent request = getIntent();
        if (DevicePolicyManager.ACTION_GET_PROVISIONING_MODE.equals(request.getAction())) {
            ArrayList<Integer> modes = request.getIntegerArrayListExtra(
                DevicePolicyManager.EXTRA_PROVISIONING_ALLOWED_PROVISIONING_MODES);
            int personalMode = DevicePolicyManager.PROVISIONING_MODE_MANAGED_PROFILE_ON_PERSONAL_DEVICE;
            int managedProfile = modes != null && modes.contains(personalMode)
                ? personalMode : DevicePolicyManager.PROVISIONING_MODE_MANAGED_PROFILE;
            if (modes != null && modes.contains(managedProfile)) {
                setResult(RESULT_OK, new Intent().putExtra(DevicePolicyManager.EXTRA_PROVISIONING_MODE, managedProfile));
            }
        } else {
            try {
                if (SpaceProfile.configure(this)) setResult(RESULT_OK);
            } catch (SecurityException error) {
                Log.w("FrameRoomSpace", "O perfil não pôde ser ativado", error);
            }
        }
        finish();
    }
}
