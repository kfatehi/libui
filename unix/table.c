// 26 june 2016
#include "uipriv_unix.h"
#include "table.h"

struct uiTable {
	uiUnixControl c;
	GtkWidget *widget;
	GtkContainer *scontainer;
	GtkScrolledWindow *sw;
	GtkWidget *treeWidget;
	GtkTreeView *tv;
	uiTableModel *model;
	GPtrArray *columnParams;
	int backgroundColumn;
};

// use the same size as GtkFileChooserWidget's treeview
// TODO refresh when icon theme changes
// TODO doesn't work when scaled?
// TODO is this even necessary?
static void setImageSize(GtkCellRenderer *r)
{
	gint size;
	gint width, height;
	gint xpad, ypad;

	size = 16;		// fallback used by GtkFileChooserWidget
	if (gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height) != FALSE)
		size = MAX(width, height);
	gtk_cell_renderer_get_padding(r, &xpad, &ypad);
	gtk_cell_renderer_set_fixed_size(r,
		2 * xpad + size,
		2 * ypad + size);
}

static void applyColor(GtkTreeModel *m, GtkTreeIter *iter, int modelColumn, GtkCellRenderer *r, const char *prop, const char *propSet)
{
	GValue value = G_VALUE_INIT;
	GdkRGBA *rgba;

	gtk_tree_model_get_value(m, iter, modelColumn, &value);
	rgba = (GdkRGBA *) g_value_get_boxed(&value);
	if (rgba != NULL)
		g_object_set(r, prop, rgba, NULL);
	else
		g_object_set(r, propSet, FALSE, NULL);
	g_value_unset(&value);
}

static void setEditable(uiTableModel *m, GtkTreeIter *iter, int modelColumn, GtkCellRenderer *r, const char *prop)
{
	GValue value = G_VALUE_INIT;
	gboolean editable;

	switch (modelColumn) {
	case uiTableModelColumnNeverEditable:
		editable = FALSE;
		break;
	case uiTableModelColumnAlwaysEditable:
		editable = TRUE;
		break;
	default:
		gtk_tree_model_get_value(GTK_TREE_MODEL(m), iter, modelColumn, &value);
		editable = g_value_get_int(&value) != 0;
		g_value_unset(&value);
	}
	g_object_set(r, prop, editable, NULL);
}

static void applyBackgroundColor(uiTable *t, GtkTreeModel *m, GtkTreeIter *iter, GtkCellRenderer *r)
{
	if (t->backgroundColumn != -1)
		applyColor(m, iter, t->backgroundColumn,
			r, "cell-background-rgba", "cell-background-set");
}

static void onEdited(uiTableModel *m, int column, const char *pathstr, const uiTableData *data, GtkTreeIter *iter)
{
	GtkTreePath *path;
	int row;

	path = gtk_tree_path_new_from_string(pathstr);
	row = gtk_tree_path_get_indices(path)[0];
	if (iter != NULL)
		gtk_tree_model_get_iter(GTK_TREE_MODEL(m), iter, path);
	gtk_tree_path_free(path);
	(*(m->mh->SetCellValue))(m->mh, m, row, column, data);
}

// TODO deduplicate this between platforms
static uiTableTextColumnOptionalParams defaultTextColumnOptionalParams = {
	.ColorModelColumn = -1,
};

struct textColumnParams {
	uiTable *t;
	uiTableModel *m;
	int modelColumn;
	int editableColumn;
	uiTableTextColumnOptionalParams params;
};

static void textColumnDataFunc(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *iter, gpointer data)
{
	struct textColumnParams *p = (struct textColumnParams *) data;
	GValue value = G_VALUE_INIT;
	const gchar *str;

	gtk_tree_model_get_value(m, iter, p->modelColumn, &value);
	str = g_value_get_string(&value);
	g_object_set(r, "text", str, NULL);
	g_value_unset(&value);

	setEditable(p->m, iter, p->editableColumn, r, "editable");

	if (p->params.ColorModelColumn != -1)
		applyColor(m, iter, p->params.ColorModelColumn,
			r, "foreground-rgba", "foreground-set");

	applyBackgroundColor(p->t, m, iter, r);
}

static void textColumnEdited(GtkCellRendererText *r, gchar *path, gchar *newText, gpointer data)
{
	struct textColumnParams *p = (struct textColumnParams *) data;
	uiTableData *tdata;
	GtkTreeIter iter;

	tdata = uiNewTableDataString(newText);
	onEdited(p->m, p->modelColumn, path, tdata, &iter);
	uiFreeTableData(tdata);
	// and update the column TODO copy comment here
	textColumnDataFunc(NULL, GTK_CELL_RENDERER(r), GTK_TREE_MODEL(p->m), &iter, data);
}

struct imageColumnParams {
	uiTable *t;
	int modelColumn;
};

static void imageColumnDataFunc(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *iter, gpointer data)
{
	struct imageColumnParams *p = (struct imageColumnParams *) data;
	GValue value = G_VALUE_INIT;
	uiImage *img;

//TODO	setImageSize(r);
	gtk_tree_model_get_value(m, iter, p->modelColumn, &value);
	img = (uiImage *) g_value_get_pointer(&value);
	g_object_set(r, "surface",
		uiprivImageAppropriateSurface(img, p->t->treeWidget),
		NULL);
	g_value_unset(&value);

	applyBackgroundColor(p->t, m, iter, r);
}

struct checkboxColumnParams {
	uiTable *t;
	uiTableModel *m;
	int modelColumn;
	int editableColumn;
};

static void checkboxColumnDataFunc(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *iter, gpointer data)
{
	struct checkboxColumnParams *p = (struct checkboxColumnParams *) data;
	GValue value = G_VALUE_INIT;
	gboolean active;

	gtk_tree_model_get_value(m, iter, p->modelColumn, &value);
	active = g_value_get_int(&value) != 0;
	g_object_set(r, "active", active, NULL);
	g_value_unset(&value);

	setEditable(p->m, iter, p->editableColumn, r, "activatable");

	applyBackgroundColor(p->t, m, iter, r);
}

static void checkboxColumnToggled(GtkCellRendererToggle *r, gchar *pathstr, gpointer data)
{
	struct checkboxColumnParams *p = (struct checkboxColumnParams *) data;
	GValue value = G_VALUE_INIT;
	int v;
	uiTableData *tdata;
	GtkTreePath *path;
	GtkTreeIter iter;

	path = gtk_tree_path_new_from_string(pathstr);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(p->m), &iter, path);
	gtk_tree_path_free(path);
	gtk_tree_model_get_value(GTK_TREE_MODEL(p->m), &iter, p->modelColumn, &value);
	v = g_value_get_int(&value);
	g_value_unset(&value);
	tdata = uiNewTableDataInt(!v);
	onEdited(p->m, p->modelColumn, pathstr, tdata, NULL);
	uiFreeTableData(tdata);
	// and update the column TODO copy comment here
	// TODO avoid fetching the model data twice
	checkboxColumnDataFunc(NULL, GTK_CELL_RENDERER(r), GTK_TREE_MODEL(p->m), &iter, data);
}

struct progressBarColumnParams {
	uiTable *t;
	int modelColumn;
};

static void progressBarColumnDataFunc(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *iter, gpointer data)
{
	struct progressBarColumnParams *p = (struct progressBarColumnParams *) data;
	GValue value = G_VALUE_INIT;
	int pval;

	gtk_tree_model_get_value(m, iter, p->modelColumn, &value);
	pval = g_value_get_int(&value);
	if (pval == -1) {
		// TODO
	} else
		g_object_set(r,
			"pulse", -1,
			"value", pval,
			NULL);
	g_value_unset(&value);

	applyBackgroundColor(p->t, m, iter, r);
}

struct buttonColumnParams {
	uiTable *t;
	uiTableModel *m;
	int modelColumn;
	int clickableColumn;
};

static void buttonColumnDataFunc(GtkTreeViewColumn *c, GtkCellRenderer *r, GtkTreeModel *m, GtkTreeIter *iter, gpointer data)
{
	struct buttonColumnParams *p = (struct buttonColumnParams *) data;
	GValue value = G_VALUE_INIT;
	const gchar *str;

	gtk_tree_model_get_value(m, iter, p->modelColumn, &value);
	str = g_value_get_string(&value);
	g_object_set(r, "text", str, NULL);
	g_value_unset(&value);

	setEditable(p->m, iter, p->clickableColumn, r, "sensitive");

	applyBackgroundColor(p->t, m, iter, r);
}

// TODO wrong type here
static void buttonColumnClicked(GtkCellRenderer *r, gchar *pathstr, gpointer data)
{
	struct buttonColumnParams *p = (struct buttonColumnParams *) data;

	onEdited(p->m, p->modelColumn, pathstr, NULL, NULL);
}

static GtkTreeViewColumn *addColumn(uiTable *t, const char *name)
{
	GtkTreeViewColumn *c;

	c = gtk_tree_view_column_new();
	gtk_tree_view_column_set_resizable(c, TRUE);
	gtk_tree_view_column_set_title(c, name);
	gtk_tree_view_append_column(t->tv, c);
	return c;
}

static void addTextColumn(uiTable *t, GtkTreeViewColumn *c, int textModelColumn, int textEditableModelColumn, uiTableTextColumnOptionalParams *params)
{
	struct textColumnParams *p;
	GtkCellRenderer *r;

	p = uiprivNew(struct textColumnParams);
	p->t = t;
	// TODO get rid of these fields AND rename t->model in favor of t->m
	p->m = t->model;
	p->modelColumn = textModelColumn;
	p->editableColumn = textEditableModelColumn;
	if (params != NULL)
		p->params = *params;
	else
		p->params = defaultTextColumnOptionalParams;

	r = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(c, r, TRUE);
	gtk_tree_view_column_set_cell_data_func(c, r, textColumnDataFunc, p, NULL);
	g_signal_connect(r, "edited", G_CALLBACK(textColumnEdited), p);
	g_ptr_array_add(t->columnParams, p);
}

// TODO rename modelCOlumn and params everywhere
void uiTableAppendTextColumn(uiTable *t, const char *name, int textModelColumn, int textEditableModelColumn, uiTableTextColumnOptionalParams *params)
{
	GtkTreeViewColumn *c;

	c = addColumn(t, name);
	addTextColumn(t, c, textModelColumn, textEditableModelColumn, params);
}

static void addImageColumn(uiTable *t, GtkTreeViewColumn *c, int imageModelColumn)
{
	struct imageColumnParams *p;
	GtkCellRenderer *r;

	p = uiprivNew(struct imageColumnParams);
	p->t = t;
	p->modelColumn = imageModelColumn;

	r = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(c, r, FALSE);
	gtk_tree_view_column_set_cell_data_func(c, r, imageColumnDataFunc, p, NULL);
	g_ptr_array_add(t->columnParams, p);
}

void uiTableAppendImageColumn(uiTable *t, const char *name, int imageModelColumn)
{
	GtkTreeViewColumn *c;

	c = addColumn(t, name);
	addImageColumn(t, c, imageModelColumn);
}

void uiTableAppendImageTextColumn(uiTable *t, const char *name, int imageModelColumn, int textModelColumn, int textEditableModelColumn, uiTableTextColumnOptionalParams *textParams)
{
	GtkTreeViewColumn *c;

	c = addColumn(t, name);
	addImageColumn(t, c, imageModelColumn);
	addTextColumn(t, c, textModelColumn, textEditableModelColumn, textParams);
}

static void addCheckboxColumn(uiTable *t, GtkTreeViewColumn *c, int checkboxModelColumn, int checkboxEditableModelColumn)
{
	struct checkboxColumnParams *p;
	GtkCellRenderer *r;

	p = uiprivNew(struct checkboxColumnParams);
	p->t = t;
	p->m = t->model;
	p->modelColumn = checkboxModelColumn;
	p->editableColumn = checkboxEditableModelColumn;

	r = gtk_cell_renderer_toggle_new();
	gtk_tree_view_column_pack_start(c, r, FALSE);
	gtk_tree_view_column_set_cell_data_func(c, r, checkboxColumnDataFunc, p, NULL);
	g_signal_connect(r, "toggled", G_CALLBACK(checkboxColumnToggled), p);
	g_ptr_array_add(t->columnParams, p);
}

void uiTableAppendCheckboxColumn(uiTable *t, const char *name, int checkboxModelColumn, int checkboxEditableModelColumn)
{
	GtkTreeViewColumn *c;

	c = addColumn(t, name);
	addCheckboxColumn(t, c, checkboxModelColumn, checkboxEditableModelColumn);
}

void uiTableAppendCheckboxTextColumn(uiTable *t, const char *name, int checkboxModelColumn, int checkboxEditableModelColumn, int textModelColumn, int textEditableModelColumn, uiTableTextColumnOptionalParams *textParams)
{
	GtkTreeViewColumn *c;

	c = addColumn(t, name);
	addCheckboxColumn(t, c, checkboxModelColumn, checkboxEditableModelColumn);
	addTextColumn(t, c, textModelColumn, textEditableModelColumn, textParams);
}

void uiTableAppendProgressBarColumn(uiTable *t, const char *name, int progressModelColumn)
{
	GtkTreeViewColumn *c;
	struct progressBarColumnParams *p;
	GtkCellRenderer *r;

	c = addColumn(t, name);

	p = uiprivNew(struct progressBarColumnParams);
	p->t = t;
	// TODO make progress and progressBar consistent everywhere
	p->modelColumn = progressModelColumn;

	r = gtk_cell_renderer_progress_new();
	gtk_tree_view_column_pack_start(c, r, TRUE);
	gtk_tree_view_column_set_cell_data_func(c, r, progressBarColumnDataFunc, p, NULL);
	g_ptr_array_add(t->columnParams, p);
}

void uiTableAppendButtonColumn(uiTable *t, const char *name, int buttonTextModelColumn, int buttonClickableModelColumn)
{
	GtkTreeViewColumn *c;
	struct buttonColumnParams *p;
	GtkCellRenderer *r;

	c = addColumn(t, name);

	p = uiprivNew(struct buttonColumnParams);
	p->t = t;
	p->m = t->model;
	p->modelColumn = buttonTextModelColumn;
	p->clickableColumn = buttonClickableModelColumn;

	r = uiprivNewCellRendererButton();
	gtk_tree_view_column_pack_start(c, r, TRUE);
	gtk_tree_view_column_set_cell_data_func(c, r, buttonColumnDataFunc, p, NULL);
	g_signal_connect(r, "clicked", G_CALLBACK(buttonColumnClicked), p);
	g_ptr_array_add(t->columnParams, p);
}

uiUnixControlAllDefaultsExceptDestroy(uiTable)

static void uiTableDestroy(uiControl *c)
{
	uiTable *t = uiTable(c);

	// TODO
	g_object_unref(t->widget);
	uiFreeControl(uiControl(t));
}

void uiTableSetRowBackgroundColorModelColumn(uiTable *t, int modelColumn)
{
	t->backgroundColumn = modelColumn;
	// TODO refresh table
}

uiTable *uiNewTable(uiTableModel *model)
{
	uiTable *t;

	uiUnixNewControl(uiTable, t);

	t->model = model;
	t->columnParams = g_ptr_array_new();
	t->backgroundColumn = -1;

	t->widget = gtk_scrolled_window_new(NULL, NULL);
	t->scontainer = GTK_CONTAINER(t->widget);
	t->sw = GTK_SCROLLED_WINDOW(t->widget);
	gtk_scrolled_window_set_shadow_type(t->sw, GTK_SHADOW_IN);

	t->treeWidget = gtk_tree_view_new_with_model(GTK_TREE_MODEL(t->model));
	t->tv = GTK_TREE_VIEW(t->treeWidget);
	// TODO set up t->tv

	gtk_container_add(t->scontainer, t->treeWidget);
	// and make the tree view visible; only the scrolled window's visibility is controlled by libui
	gtk_widget_show(t->treeWidget);

	return t;
}