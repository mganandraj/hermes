package com.facebook.hermes.intltest;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.os.Handler;
import android.text.Html;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.ArrayAdapter;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.TextView;

import java.util.ArrayList;

class DoubleTapReloadRecognizer {
    private boolean mDoRefresh = false;
    private static final long DOUBLE_TAP_DELAY = 200;

    public boolean didDoubleTapEnter(int keyCode, View view) {
        if (keyCode == KeyEvent.KEYCODE_ENTER && view instanceof EditText) {
            if (mDoRefresh) {
                mDoRefresh = false;
                return true;
            } else {
                mDoRefresh = true;
                new Handler()
                        .postDelayed(
                                new Runnable() {
                                    @Override
                                    public void run() {
                                        mDoRefresh = false;
                                    }
                                },
                                DOUBLE_TAP_DELAY);
            }
        }
        return false;
    }
}


public class MainActivity extends Activity {

    private ListView scriptListView;
    private EditText scriptEditText;
    private CustomAdapter adapter;

    DoubleTapReloadRecognizer doubleTapReloadRecognizer = new DoubleTapReloadRecognizer();


    static {
        System.loadLibrary("hermes");
    }

class CustomAdapter extends BaseAdapter {
    private ArrayList<String> scriptsAndResponses;
    private Context context;

    public CustomAdapter(Context context) {
        this.scriptsAndResponses = new ArrayList<>();
        this.context = context;
    }

    public void add(String text) {
        scriptsAndResponses.add(text);
        this.notifyDataSetChanged();
    }

    @Override
    public int getCount() {
        return scriptsAndResponses.size();
    }

    @Override
    public Object getItem(int i) {
        return scriptsAndResponses.get(i);
    }

    @Override
    public long getItemId(int i) {
        return i;
    }


    @Override
    public View getView(int i, View view, ViewGroup viewGroup) {
        if (view == null) {
            view = new TextView(context);
        }

        String text = (String)getItem(i);
        ((TextView)view).setText(Html.fromHtml(text));

        return view;
    }
}


    private void evalScript(EditText scriptEditText ,ListView scriptListView, CustomAdapter adapter) {
        String script = scriptEditText.getText().toString();

        adapter.add(script);
        String result = nativeEvalScript(script);
        adapter.add(result);

        scriptListView.smoothScrollToPosition(adapter.getCount());
        scriptEditText.setText("");

        InputMethodManager inputManager = (InputMethodManager)getSystemService(Context.INPUT_METHOD_SERVICE);
        inputManager.toggleSoftInput(0, 0);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        adapter = new CustomAdapter(this);

        scriptListView = (ListView) findViewById(R.id.scriptsListView);
        scriptListView.setAdapter(adapter);

        scriptEditText= (EditText) findViewById(R.id.scriptEditText);
        scriptEditText.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override
            public boolean onEditorAction(TextView textView, int actionId, KeyEvent keyEvent) {
                boolean handled = false;
                if (actionId == EditorInfo.IME_ACTION_DONE) {
                    evalScript(scriptEditText, scriptListView, adapter);
                    handled = true;
                }
                return handled;

            }
        });

        this.registerReceiver(new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                String script = intent.getStringExtra("script");
                scriptEditText.setText(script);
            }
        }, new IntentFilter("com.facebook.hermes.intltest.eval"));

        Button evalButton = (Button)findViewById(R.id.evalButton);
        evalButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                String script = scriptEditText.getText().toString();
                evalScript(scriptEditText, scriptListView, adapter);
            }
        });
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if(doubleTapReloadRecognizer.didDoubleTapEnter(keyCode, this.getCurrentFocus())) {
            evalScript(scriptEditText, scriptListView, adapter);
        }

        return super.onKeyUp(keyCode, event);
    }

    static native String nativeEvalScript(String s);
}