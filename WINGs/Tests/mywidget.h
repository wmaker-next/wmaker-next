/*
 * Header for demo widget.
 *
 * Author: Alfredo K. Kojima
 *
 * This file is in the public domain.
 */

typedef struct W_MyWidget MyWidget;


MyWidget *CreateMyWidget(WMWidget *parent);

void SetMyWidgetText(MyWidget *mPtr, char *text);

W_Class InitMyWidget(WMScreen *scr);

