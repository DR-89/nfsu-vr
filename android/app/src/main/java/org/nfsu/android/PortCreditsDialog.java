package org.nfsu.android;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.text.Html;
import android.text.method.LinkMovementMethod;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.TextView;

/** Credits use the launcher's artwork and native, accessible Android links. */
final class PortCreditsDialog {
    private PortCreditsDialog() { }

    static Dialog show(Activity activity) {
        final Dialog dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setContentView(R.layout.dialog_port_credits);
        TextView text = dialog.findViewById(R.id.port_credits_text);
        text.setText(Html.fromHtml(activity.getString(R.string.port_credits_body,
                BuildConfig.BUILD_COMMIT, BuildConfig.BUILD_COMMIT_FULL),
                Html.FROM_HTML_MODE_LEGACY));
        text.setMovementMethod(LinkMovementMethod.getInstance());
        Button close = dialog.findViewById(R.id.port_credits_close);
        close.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View view) { dialog.dismiss(); }
        });
        Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        }
        dialog.show();
        if (window != null) {
            int width = Math.round(520 * activity.getResources().getDisplayMetrics().density);
            android.view.WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(android.view.WindowInsetsController
                        .BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(android.view.WindowInsets.Type.systemBars());
            }
            window.setLayout(Math.min(width,
                    activity.getResources().getDisplayMetrics().widthPixels),
                    WindowManager.LayoutParams.WRAP_CONTENT);
        }
        return dialog;
    }
}
