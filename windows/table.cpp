#include "uipriv_windows.hpp"

struct uiTableModel {
	uiTableModelHandler *mh;
	std::vector<uiTable *> *tables;
};

struct uiTableColumn {
	uiTable *t;
	WCHAR *name;
	// don't really support parts (but this would part=>column mappings if we did)
	int modelColumn;	// -1 = none
};

struct uiTable {
	uiWindowsControl c;
	uiTableModel *model;
	HWND hwnd;
	std::vector<uiTableColumn *> *columns;
};

uiTableModel *uiNewTableModel(uiTableModelHandler *mh)
{
	uiTableModel *m;

	m = uiprivNew(uiTableModel);
	m->mh = mh;
	m->tables = new std::vector<uiTable *>;
	return m;
}

void uiFreeTableModel(uiTableModel *m)
{
	delete m->tables;
	uiprivFree(m);
}

// TODO document that when this is called, the model must return the new row count when asked
void uiTableModelRowInserted(uiTableModel *m, int newIndex)
{
	LVITEMW item;
	int newCount;

	newCount = (*(m->mh->NumRows))(m->mh, m);
	ZeroMemory(&item, sizeof (LVITEMW));
	item.mask = 0;
	item.iItem = newIndex;
	item.iSubItem = 0;
	for (auto t : *(m->tables)) {
		// actually insert the rows
		if (SendMessageW(t->hwnd, LVM_SETITEMCOUNT, (WPARAM) newCount, LVSICF_NOINVALIDATEALL) == 0)
			logLastError(L"error calling LVM_SETITEMCOUNT in uiTableModelRowInserted()");
		// and redraw every row from the new row down to simulate adding it
		if (SendMessageW(t->hwnd, LVM_REDRAWITEMS, (WPARAM) newIndex, (LPARAM) (newCount - 1)) == FALSE)
			logLastError(L"error calling LVM_REDRAWITEMS in uiTableModelRowInserted()");

		// update selection state
		if (SendMessageW(t->hwnd, LVM_INSERTITEM, 0, (LPARAM) (&item)) == (LRESULT) (-1))
			logLastError(L"error calling LVM_INSERTITEM in uiTableModelRowInserted() to update selection state");
	}
}

// TODO compare LVM_UPDATE and LVM_REDRAWITEMS
void uiTableModelRowChanged(uiTableModel *m, int index)
{
	for (auto t : *(m->tables))
		if (SendMessageW(t->hwnd, LVM_UPDATE, (WPARAM) index, 0) == (LRESULT) (-1))
			logLastError(L"error calling LVM_UPDATE in uiTableModelRowChanged()");
}

// TODO document that when this is called, the model must return the OLD row count when asked
// TODO for this and the above, see what GTK+ requires and adjust accordingly
void uiTableModelRowDeleted(uiTableModel *m, int oldIndex)
{
	int newCount;

	newCount = (*(m->mh->NumRows))(m->mh, m);
	newCount--;
	for (auto t : *(m->tables)) {
		// update selection state
		if (SendMessageW(t->hwnd, LVM_DELETEITEM, (WPARAM) oldIndex, 0) == (LRESULT) (-1))
			logLastError(L"error calling LVM_DELETEITEM in uiTableModelRowDeleted() to update selection state");

		// actually delete the rows
		if (SendMessageW(t->hwnd, LVM_SETITEMCOUNT, (WPARAM) newCount, LVSICF_NOINVALIDATEALL) == 0)
			logLastError(L"error calling LVM_SETITEMCOUNT in uiTableModelRowDeleted()");
		// and redraw every row from the new nth row down to simulate removing the old nth row
		if (SendMessageW(t->hwnd, LVM_REDRAWITEMS, (WPARAM) oldIndex, (LPARAM) (newCount - 1)) == FALSE)
			logLastError(L"error calling LVM_REDRAWITEMS in uiTableModelRowDeleted()");
	}
}

void uiTableColumnAppendTextPart(uiTableColumn *c, int modelColumn, int expand)
{
	uiTable *t = c->t;
	int lvIndex = 0;
	LVCOLUMNW lvc;

	if (c->modelColumn >= 0)
		return; // multiple parts not implemented
	c->modelColumn = modelColumn;

	// work out appropriate listview index for the column
	for (auto candidate : *(t->columns)) {
		if (candidate == c)
			break;
		if (candidate->modelColumn >= 0)
			lvIndex++;
	}

	ZeroMemory(&lvc, sizeof (LVCOLUMNW));
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT;	/* | LVCF_SUBITEM; */
	lvc.fmt = LVCFMT_LEFT;
	lvc.cx = 120;	// TODO
	lvc.pszText = c->name;
	if (SendMessageW(c->t->hwnd, LVM_INSERTCOLUMN, (WPARAM) lvIndex, (LPARAM) (&lvc)) == (LRESULT) (-1))
		logLastError(L"error calling LVM_INSERTCOLUMN in uiTableColumnPartSetTextPart()");
}

void uiTableColumnAppendImagePart(uiTableColumn *c, int modelColumn, int expand)
{
	// not implemented
}

void uiTableColumnAppendButtonPart(uiTableColumn *c, int modelColumn, int expand)
{
	// not implemented
}

void uiTableColumnAppendCheckboxPart(uiTableColumn *c, int modelColumn, int expand)
{
	// not implemented
}

void uiTableColumnAppendProgressBarPart(uiTableColumn *c, int modelColumn, int expand)
{
	// not implemented
}

void uiTableColumnPartSetEditable(uiTableColumn *c, int part, int editable)
{
	// TODO
}

void uiTableColumnPartSetTextColor(uiTableColumn *c, int part, int modelColumn)
{
	// not implemented
}

// uiTable implementation

uiWindowsControlAllDefaultsExceptDestroy(uiTable)

uiTableColumn *uiTableAppendColumn(uiTable *t, const char *name)
{
	uiTableColumn *c;
   
	c = uiprivNew(uiTableColumn);
	c->name = toUTF16(name);
	c->t = t;
	c->modelColumn = -1;	// -1 = unassigned
	// we defer the actual ListView_InsertColumn call until a part is added...
	t->columns->push_back(c);
	return c;
}

void uiTableSetRowBackgroundColorModelColumn(uiTable *t, int modelColumn)
{
	// not implemented
}

static void uiTableDestroy(uiControl *c)
{
	uiTable *t = uiTable(c);
	uiTableModel *model = t->model;
	std::vector<uiTable *>::iterator it;

	uiWindowsUnregisterWM_NOTIFYHandler(t->hwnd);
	uiWindowsEnsureDestroyWindow(t->hwnd);
	// detach table from model
	for (it = model->tables.begin(); it != model->tables.end(); it++) {
		if (*it == t) {
			model->tables.erase(it);
			break;
		}
	}
	// free the columns
	for (auto col : *(t->columns)) {
		uiprivFree(col->name);
		uiprivFree(col);
	}
	delete t->columns;
	uiFreeControl(uiControl(t));
}

// suggested listview sizing from http://msdn.microsoft.com/en-us/library/windows/desktop/dn742486.aspx#sizingandspacing:
// "columns widths that avoid truncated data x an integral number of items"
// Don't think that'll cut it when some cells have overlong data (eg
// stupidly long URLs). So for now, just hardcode a minimum.
// TODO: Investigate using LVM_GETHEADER/HDM_LAYOUT here...
#define tableMinWidth 107		/* in line with other controls */
#define tableMinHeight (14*3)	/* header + 2 lines (roughly) */

static void uiTableMinimumSize(uiWindowsControl *c, int *width, int *height)
{
	uiTable *t = uiTable(c);
	uiWindowsSizing sizing;
	int x, y;

	x = tableMinWidth;
	y = tableMinHeight;
	uiWindowsGetSizing(t->hwnd, &sizing);
	uiWindowsSizingDlgUnitsToPixels(&sizing, &x, &y);
	*width = x;
	*height = y;
}

static BOOL onWM_NOTIFY(uiControl *c, HWND hwnd, NMHDR *nmhdr, LRESULT *lResult)
{
	uiTable *t = uiTable(c);
	uiTableModelHandler *mh = t->model->mh;
	BOOL ret = FALSE;

	switch (nmhdr->code) {
	case LVN_GETDISPINFO:
	{
		NMLVDISPINFOW *di;
		LVITEMW *item;
		int row, col;
		uiTableColumn *tc;
		int mcol;
		uiTableModelColumnType typ;

		di = (NMLVDISPINFOW *)nmhdr;
		item = &(di->item);
		if (!(item->mask & LVIF_TEXT))
			break;
		row = item->iItem;
		col = item->iSubItem;
		if (col < 0 || col >= (int)t->columns->size())
			break;
		tc = (uiTableColumn *)t->columns->at(col);
		mcol = tc->modelColumn;
		typ = (*mh->ColumnType)(mh, t->model, mcol);

		if (typ == uiTableModelColumnString) {
			void* data;
			int n;

			data = (*(mh->CellValue))(mh, t->model, row, mcol);
			n = MultiByteToWideChar(CP_UTF8, 0, (const char *)data, -1, item->pszText, item->cchTextMax);
			// make sure clipped strings are nul-terminated
			if (n >= item->cchTextMax)
				item->pszText[item->cchTextMax-1] = L'\0';
		} else
			item->pszText[0] = L'\0';
		break;
	}
	}
	*lResult = 0;
	return ret;
}

uiTable *uiNewTable(uiTableModel *model)
{
	uiTable *t;
	int n;

	uiWindowsNewControl(uiTable, t);

	t->columns = new std::vector<uiTableColumn *>;
	t->model = model;
	t->hwnd = uiWindowsEnsureCreateControlHWND(WS_EX_CLIENTEDGE,
		WC_LISTVIEW, L"",
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL,
		hInstance, NULL,
		TRUE);
	model->tables.push_back(t);
	uiWindowsRegisterWM_NOTIFYHandler(t->hwnd, onWM_NOTIFY, uiControl(t));

	// TODO: try LVS_EX_AUTOSIZECOLUMNS
	SendMessageW(t->hwnd, LVM_SETEXTENDEDLISTVIEWSTYLE,
		(WPARAM) (LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP),
		(LPARAM) (LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP));
	n = (*(model->mh->NumRows))(model->mh, model);
	if (SendMessageW(t->hwnd, LVM_SETITEMCOUNT, (WPARAM) n, 0) == 0)
		logLastError(L"error calling LVM_SETITEMCOUNT in uiNewTable()");
	return t;
}
