package org.libsdl.app;

import android.content.Context;
import android.view.KeyEvent;

import java.util.function.Predicate;

/** Gives the game's chat bindings first use of controller buttons while the IME is open. */
public final class TpwTextEdit extends SDLDummyEdit {
    private final Predicate<KeyEvent> chatBinding;

    public TpwTextEdit(Context context, Predicate<KeyEvent> chatBinding) {
        super(context);
        this.chatBinding = chatBinding;
    }

    @Override
    public boolean onKeyPreIme(int keyCode, KeyEvent event) {
        return chatBinding.test(event) || super.onKeyPreIme(keyCode, event);
    }
}
