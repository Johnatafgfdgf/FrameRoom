package dev.frameroom;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

/** The only activities forwarded across the profile boundary are our own. */
public final class SpaceBridgeActivity extends Activity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (!SpaceProfile.isOwned(this)) {
            Toast.makeText(this, "Abra o perfil FrameRoom para continuar.", Toast.LENGTH_LONG).show();
            finish();
            return;
        }
        Intent incoming = getIntent();
        if (SpaceProfile.action(this, "INSTALL").equals(incoming.getAction())) {
            startActivity(new Intent(this, SpaceInstallActivity.class)
                .putExtra(SpaceProfile.PACKAGE, incoming.getStringExtra(SpaceProfile.PACKAGE))
                .putExtra(SpaceProfile.APP_LABEL, incoming.getStringExtra(SpaceProfile.APP_LABEL)));
        } else if (SpaceProfile.action(this, "OPEN").equals(incoming.getAction())) {
            startActivity(new Intent(this, ParallelSpaceActivity.class));
        }
        finish();
    }
}
