

#include "pebble.h"

static Window *window;

static Window *menu_window;

static TextLayer *project_layer;

static TextLayer *task_layer;

static TextLayer *timer_layer;

static SimpleMenuLayer *menu_list_layer;

static DictionaryIterator *iter;


// Time values
static int elapsed_time; // diff for now - start time
static int start_time;

// Bool flag for running timer
static bool running;

// fonts
static GFont timer_font;
static GFont project_font;
static GFont task_font;

// holders for menu entries
SimpleMenuSection list_menu_sections[1];
SimpleMenuItem* list_menu_items;
char* menu_action;
int page;
char* project_title;
char* task_title;

// Function decs
void timer_callback(struct tm *tick_time, TimeUnits units_changed);
void update_timer_layer();
void start_timer();
void stop_timer();
static void click_config_provider(void* context);
void toggle_timer_click(ClickRecognizerRef recognizer, Window *window);
void cancel_timer_click(ClickRecognizerRef recognizer, Window *window);
void change_task_click(ClickRecognizerRef recognizer, Window *window);
void change_project_click(ClickRecognizerRef recognizer, Window *window);
void change_client_click(ClickRecognizerRef recognizer, Window *window);
void submit_time_to_task(ClickRecognizerRef recognizer, Window *window);
static void init(void);
static void deinit(void);
int main(void);
static void in_received_handler(DictionaryIterator *received, void *context);
static void in_dropped_handler(AppMessageResult reason, void *context);
static void out_sent_handler(DictionaryIterator *sent, void *context);
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context);
void select_menu_callback (int index, void *context);


// check if timer is running, and update display if so, always restart the timer loop
void timer_callback(struct tm *tick_time, TimeUnits units_changed) {
	if(running) {
		static int now;
		now = time(NULL);
		elapsed_time = now - start_time;
		update_timer_layer();
	}
}


//Update the display
void update_timer_layer() {
	static char time_display[] = "00h 00m";
	// Get hours and minutes for display
	int minutes = (int)elapsed_time / 60 % 60;
	int hours = (int)elapsed_time / 3600;
	snprintf(time_display, 8, "%02dh %02dm", hours, minutes);
	text_layer_set_text(timer_layer, time_display);
}


void start_timer() {
	running = true;
	if(start_time == 0) { // shouldn't be needed, just in case of unhandled exceptions
		start_time = time(NULL);
	}
	tick_timer_service_subscribe(MINUTE_UNIT, timer_callback);
}

void stop_timer() {
    running = false;
			tick_timer_service_unsubscribe();
}

// set up button clicks or base window
static void click_config_provider(void* context) {
	window_single_click_subscribe(BUTTON_ID_SELECT, (ClickHandler)toggle_timer_click);
	window_single_click_subscribe(BUTTON_ID_UP, (ClickHandler)cancel_timer_click);
	window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler)submit_time_to_task);
	window_long_click_subscribe(BUTTON_ID_DOWN, 700, (ClickHandler)change_task_click, NULL);
	window_long_click_subscribe(BUTTON_ID_UP, 700, (ClickHandler)change_client_click, NULL);
	window_long_click_subscribe(BUTTON_ID_SELECT, 700, (ClickHandler)change_project_click, NULL);
}

// Button handlers
// Start/stop timer
void toggle_timer_click(ClickRecognizerRef recognizer, Window *window) {
    if(running) {
			stop_timer();
    } else {
			start_timer();
    }
}

// Cancel current time entry
void cancel_timer_click(ClickRecognizerRef recognizer, Window *window) {
	if (running) {
		// currently just acts as timer stop, will later change this to present a are you sure prompt, as it likely is an incorrect click
		stop_timer();
	} else {
		elapsed_time = 0;
		start_time = 0;
		update_timer_layer();
	}
}
// Select another task for the current project, or create a new (unnamed) task
void change_task_click(ClickRecognizerRef recognizer, Window *window) {
	app_message_outbox_begin(&iter);
	Tuplet value = TupletCString(0, "getTasks");
	dict_write_tuplet(iter, &value);
	Tuplet pager = TupletInteger(1, page);
	dict_write_tuplet(iter, &pager);
	app_message_outbox_send();
}
// Select another project for current client
void change_project_click(ClickRecognizerRef recognizer, Window *window) {
	app_message_outbox_begin(&iter);
	Tuplet value = TupletCString(0, "getProjects");
	dict_write_tuplet(iter, &value);
	Tuplet pager = TupletInteger(1, page);
	dict_write_tuplet(iter, &pager);
	app_message_outbox_send();
}
// Change to another client
void change_client_click(ClickRecognizerRef recognizer, Window *window) {
	app_message_outbox_begin(&iter);
	Tuplet value = TupletCString(0, "getClients");
	dict_write_tuplet(iter, &value);
	Tuplet pager = TupletInteger(1, page);
	dict_write_tuplet(iter, &pager);
	app_message_outbox_send();
}

void submit_time_to_task(ClickRecognizerRef recognizer, Window *window) {
	app_message_outbox_begin(&iter);
	Tuplet value = TupletCString(0, "postTask");
	dict_write_tuplet(iter, &value);
	Tuplet start = TupletInteger(1, start_time);
	dict_write_tuplet(iter, &start);
	Tuplet duration = TupletInteger(1, elapsed_time);
	dict_write_tuplet(iter, &duration);
	app_message_outbox_send();
}

static void menu_window_unload(Window *window) {
  simple_menu_layer_destroy(menu_list_layer);
}

static void menu_window_load(Window *window) {
	// No actions needed - may refactor creation of layers to here instead of the appmessage handler
}


// Appmessage callbacks
void out_sent_handler(DictionaryIterator *sent, void *context) {
   // outgoing message was delivered
 }


 void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
   // outgoing message failed
	 APP_LOG(APP_LOG_LEVEL_WARNING, "Appmessage was not delivered");
 }


 void in_received_handler(DictionaryIterator *received, void *context) {
	int count = 0;
	Tuple *tuple = dict_read_first(received);
	static char* section_title = "Menu title";
	while(tuple){
			count ++;
			if (tuple->key == 0) {
				menu_action = tuple->value->cstring;
			}
			tuple = dict_read_next(received);
	}
	int number_of_entries = count - 1;
	list_menu_items = malloc(number_of_entries * sizeof(SimpleMenuItem));
	int i = 0;
	tuple = dict_read_first(received);
	while (tuple) {
			if(tuple->key != 0){
					list_menu_items[i] = (SimpleMenuItem){
							.title = tuple->value->cstring,
							.callback = select_menu_callback
					};
					i++;
			}
			tuple = dict_read_next(received);
	}
	if  (strcmp(menu_action, "c") == 0) {
			section_title = "Clients";
	} else if(strcmp(menu_action, "t") == 0) {
			section_title = "Tasks";
	} else if (strcmp(menu_action, "p") == 0) {
			section_title = "Projects";
	} else {
			section_title = "Select";
	}
	list_menu_sections[0] = (SimpleMenuSection){
			.title = section_title,
			.num_items = number_of_entries,
			.items = list_menu_items
	};
	window_stack_push(menu_window, true);

	Layer *window_layer = window_get_root_layer(menu_window);
	menu_list_layer = simple_menu_layer_create(GRect(0, 5, 144, 163), menu_window, list_menu_sections, 1, NULL);
	layer_add_child(window_layer, simple_menu_layer_get_layer(menu_list_layer));
 }


 void in_dropped_handler(AppMessageResult reason, void *context) {
   // incoming message dropped
 }


 // Menu callback
 void select_menu_callback(int index, void *context) {
	app_message_outbox_begin(&iter);
	static char* select_action;
	if  (strcmp(menu_action, "c") == 0) {
		select_action = "selClient";
	} else if(strcmp(menu_action, "t") == 0) {
		select_action = "selTask";
		snprintf(task_title, 40, "Task:\n%s", list_menu_items[index].title);
		text_layer_set_text(task_layer, task_title);
	} else if (strcmp(menu_action, "p") == 0) {
		select_action = "selProj";
		snprintf(project_title, 40, "Project:\n%s", list_menu_items[index].title);
		text_layer_set_text(task_layer, project_title);
	} else {
		// strcmp failed to find a match
		APP_LOG(APP_LOG_LEVEL_DEBUG, "No match on strcmp, value of menu_action follows");
		APP_LOG(APP_LOG_LEVEL_DEBUG, menu_action);
	}
	Tuplet action_type = TupletCString(0, select_action);
	dict_write_tuplet(iter, &action_type);
	Tuplet selected = TupletInteger(1, index);
	dict_write_tuplet(iter, &selected);
	app_message_outbox_send(); // this send is causing crash :S

	//pop the menu off
	window_stack_pop(true);
 }

static void init(void) {
	// Set initial value to track timer start/stop
	running = false;
	// Set variables to use for animated windows
	const bool animated = true;

	// Set up the base window
  window = window_create();
  Layer *window_layer = window_get_root_layer(window);
	window_set_background_color(window, GColorWhite);
  window_set_fullscreen(window, false);
	window_stack_push(window, animated);
	page = 1; // set to 1 for initial paging on menus

	// Bind input for base window
	window_set_click_config_provider(window, (ClickConfigProvider) click_config_provider);

	// Set up layers for base window
	timer_font = fonts_get_system_font(FONT_KEY_DROID_SERIF_28_BOLD);
	timer_layer = text_layer_create(GRect(0, 80, 144, 84));
	text_layer_set_background_color(timer_layer, GColorBlack);
  text_layer_set_font(timer_layer, timer_font);
	text_layer_set_text_color(timer_layer, GColorWhite);
	text_layer_set_text(timer_layer, "00h 00m");
	text_layer_set_text_alignment(timer_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(timer_layer));

	project_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
	project_layer = text_layer_create(GRect(0, 0, 144, 40));
	text_layer_set_background_color(project_layer, GColorWhite);
  text_layer_set_font(project_layer, project_font);
	text_layer_set_text_color(project_layer, GColorBlack);
	text_layer_set_text(project_layer, "No Project Selected");
	text_layer_set_text_alignment(project_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(project_layer));

	task_font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
	task_layer = text_layer_create(GRect(0, 40, 144, 40));
	text_layer_set_background_color(task_layer, GColorWhite);
  text_layer_set_font(task_layer, task_font);
	text_layer_set_text_color(task_layer, GColorBlack);
	text_layer_set_text(task_layer, "No Task Selected");
	text_layer_set_text_alignment(task_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(task_layer));


	// Set up menu window, but don't push to stack yet
	menu_window = window_create();
	window_set_background_color(menu_window, GColorWhite);
  window_set_fullscreen(menu_window, true);
	// Window Handlers
	window_set_window_handlers(menu_window, (WindowHandlers) {
		.load = menu_window_load,
		.unload = menu_window_unload,
	});

	// Bind appmessage callbacks
	app_message_register_inbox_received(in_received_handler);
	app_message_register_inbox_dropped(in_dropped_handler);
	app_message_register_outbox_sent(out_sent_handler);
	app_message_register_outbox_failed(out_failed_handler);
	// Configure inbox/outbot for appmessage
	const uint32_t inbound_size = app_message_inbox_size_maximum();
	const uint32_t outbound_size = app_message_outbox_size_maximum();
	app_message_open(inbound_size, outbound_size);
}



static void deinit(void) {

  text_layer_destroy(timer_layer);
  text_layer_destroy(project_layer);
  text_layer_destroy(task_layer);
  window_destroy(window);
}

int main(void) {
  init();

  app_event_loop();

  deinit();
}
