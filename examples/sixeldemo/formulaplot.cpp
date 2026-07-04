/*
 * Copyright (c) 2026 by Christian Klukas
 * Licensed under the MIT License.
 */

#define Uses_TButton
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TGraphicRuntime
#define Uses_TGraphicView
#define Uses_TGroup
#define Uses_TInputLine
#define Uses_MsgBox
#define Uses_TRect
#define Uses_TStaticText
#define Uses_TWindow
#include <tvision/tv.h>

#include "formulaplot.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

const ushort cmUpdateFormulaPlot = 2101;
const ushort cmResetFormulaPlot = 2102;

struct FormulaNode
{
    enum Type
    {
        constant,
        variable,
        unaryMinus,
        add,
        subtract,
        multiply,
        divide,
        power,
        function
    };

    Type type {constant};
    double value {0.0};
    std::string name;
    std::unique_ptr<FormulaNode> left;
    std::unique_ptr<FormulaNode> right;

    FormulaNode(Type aType) : type(aType) {}
    FormulaNode(double aValue) : type(constant), value(aValue) {}

    static std::unique_ptr<FormulaNode> unary(Type type, std::unique_ptr<FormulaNode> child)
    {
        std::unique_ptr<FormulaNode> node(new FormulaNode(type));
        node->left = std::move(child);
        return node;
    }

    static std::unique_ptr<FormulaNode> binary(Type type, std::unique_ptr<FormulaNode> a,
                                               std::unique_ptr<FormulaNode> b)
    {
        std::unique_ptr<FormulaNode> node(new FormulaNode(type));
        node->left = std::move(a);
        node->right = std::move(b);
        return node;
    }

    double eval(double x) const
    {
        switch (type)
        {
        case constant:
            return value;
        case variable:
            return x;
        case unaryMinus:
            return -left->eval(x);
        case add:
            return left->eval(x) + right->eval(x);
        case subtract:
            return left->eval(x) - right->eval(x);
        case multiply:
            return left->eval(x)*right->eval(x);
        case divide:
            return left->eval(x)/right->eval(x);
        case power:
            return std::pow(left->eval(x), right->eval(x));
        case function:
        {
            double v = left->eval(x);
            if (name == "sin") return std::sin(v);
            if (name == "cos") return std::cos(v);
            if (name == "tan") return std::tan(v);
            if (name == "asin") return std::asin(v);
            if (name == "acos") return std::acos(v);
            if (name == "atan") return std::atan(v);
            if (name == "sqrt") return std::sqrt(v);
            if (name == "log" || name == "ln") return std::log(v);
            if (name == "log10") return std::log10(v);
            if (name == "exp") return std::exp(v);
            if (name == "abs") return std::fabs(v);
            if (name == "floor") return std::floor(v);
            if (name == "ceil") return std::ceil(v);
            return std::numeric_limits<double>::quiet_NaN();
        }
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
};

class FormulaParser
{
public:
    explicit FormulaParser(const std::string &input) : text(input) {}

    std::unique_ptr<FormulaNode> parse(std::string &error)
    {
        error.clear();
        std::unique_ptr<FormulaNode> result = parseExpression(error);
        skipSpaces();
        if (error.empty() && pos != text.size())
            error = std::string("Unexpected character '") + text[pos] + "'";
        if (!error.empty())
            return std::unique_ptr<FormulaNode>();
        return result;
    }

private:
    const std::string &text;
    size_t pos {0};

    void skipSpaces()
    {
        while (pos < text.size() && std::isspace((unsigned char) text[pos]))
            ++pos;
    }

    bool match(char ch)
    {
        skipSpaces();
        if (pos < text.size() && text[pos] == ch)
        {
            ++pos;
            return true;
        }
        return false;
    }

    bool startsPrimary()
    {
        skipSpaces();
        if (pos >= text.size())
            return false;
        unsigned char ch = (unsigned char) text[pos];
        return ch == '(' || ch == '.' || std::isdigit(ch) || std::isalpha(ch);
    }

    std::unique_ptr<FormulaNode> parseExpression(std::string &error)
    {
        std::unique_ptr<FormulaNode> left = parseTerm(error);
        while (error.empty())
        {
            if (match('+'))
                left = FormulaNode::binary(FormulaNode::add, std::move(left), parseTerm(error));
            else if (match('-'))
                left = FormulaNode::binary(FormulaNode::subtract, std::move(left), parseTerm(error));
            else
                break;
        }
        return left;
    }

    std::unique_ptr<FormulaNode> parseTerm(std::string &error)
    {
        std::unique_ptr<FormulaNode> left = parsePower(error);
        while (error.empty())
        {
            if (match('*'))
                left = FormulaNode::binary(FormulaNode::multiply, std::move(left), parsePower(error));
            else if (match('/'))
                left = FormulaNode::binary(FormulaNode::divide, std::move(left), parsePower(error));
            else
                break;
        }
        return left;
    }

    std::unique_ptr<FormulaNode> parsePower(std::string &error)
    {
        skipSpaces();
        if (match('+'))
            return parsePower(error);
        if (match('-'))
            return FormulaNode::unary(FormulaNode::unaryMinus, parsePower(error));

        std::unique_ptr<FormulaNode> left = parseImplicitProduct(error);
        if (error.empty() && match('^'))
            left = FormulaNode::binary(FormulaNode::power, std::move(left), parsePower(error));
        return left;
    }

    std::unique_ptr<FormulaNode> parseImplicitProduct(std::string &error)
    {
        std::unique_ptr<FormulaNode> left = parsePrimary(error);
        while (error.empty() && startsPrimary())
            left = FormulaNode::binary(FormulaNode::multiply, std::move(left), parsePrimary(error));
        return left;
    }

    std::unique_ptr<FormulaNode> parsePrimary(std::string &error)
    {
        skipSpaces();
        if (pos >= text.size())
        {
            error = "Unexpected end of formula";
            return std::unique_ptr<FormulaNode>();
        }

        if (match('('))
        {
            std::unique_ptr<FormulaNode> result = parseExpression(error);
            if (error.empty() && !match(')'))
                error = "Missing ')'";
            return result;
        }

        unsigned char ch = (unsigned char) text[pos];
        if (std::isdigit(ch) || ch == '.')
            return parseNumber(error);
        if (std::isalpha(ch))
            return parseIdentifier(error);

        error = std::string("Unexpected character '") + text[pos] + "'";
        return std::unique_ptr<FormulaNode>();
    }

    std::unique_ptr<FormulaNode> parseNumber(std::string &error)
    {
        const char *begin = text.c_str() + pos;
        char *end = 0;
        double value = std::strtod(begin, &end);
        if (end == begin)
        {
            error = "Expected number";
            return std::unique_ptr<FormulaNode>();
        }
        pos += size_t(end - begin);
        return std::unique_ptr<FormulaNode>(new FormulaNode(value));
    }

    std::unique_ptr<FormulaNode> parseIdentifier(std::string &error)
    {
        size_t begin = pos;
        while (pos < text.size() &&
               (std::isalpha((unsigned char) text[pos]) || std::isdigit((unsigned char) text[pos])))
            ++pos;
        std::string id = text.substr(begin, pos - begin);
        std::transform(id.begin(), id.end(), id.begin(),
                       [] (unsigned char c) { return char(std::tolower(c)); });

        if (id == "x")
            return std::unique_ptr<FormulaNode>(new FormulaNode(FormulaNode::variable));
        if (id == "pi")
            return std::unique_ptr<FormulaNode>(new FormulaNode(3.14159265358979323846));
        if (id == "e")
            return std::unique_ptr<FormulaNode>(new FormulaNode(2.71828182845904523536));

        if (!match('('))
        {
            error = "Function '" + id + "' needs parentheses";
            return std::unique_ptr<FormulaNode>();
        }
        std::unique_ptr<FormulaNode> arg = parseExpression(error);
        if (error.empty() && !match(')'))
            error = "Missing ')' after function argument";
        if (!error.empty())
            return std::unique_ptr<FormulaNode>();

        static const char *known[] = {
            "sin", "cos", "tan", "asin", "acos", "atan", "sqrt", "log",
            "ln", "log10", "exp", "abs", "floor", "ceil"
        };
        bool found = false;
        for (const char *name : known)
            if (id == name)
                found = true;
        if (!found)
        {
            error = "Unknown function '" + id + "'";
            return std::unique_ptr<FormulaNode>();
        }

        std::unique_ptr<FormulaNode> node(new FormulaNode(FormulaNode::function));
        node->name = id;
        node->left = std::move(arg);
        return node;
    }
};

struct PlotFormula
{
    std::string text;
    std::unique_ptr<FormulaNode> expression;
    TGraphicColor color;
};

struct PlotRange
{
    double xMin {-4.0};
    double xMax {4.0};
    double yMin {-2.0};
    double yMax {2.0};
};

static int textWidth(const char *text, int scale)
{
    int len = int(std::strlen(text));
    if (len == 0)
        return 0;
    return len*3*scale + (len - 1)*scale;
}

static const char *glyph(char ch)
{
    switch (ch)
    {
    case '0': return "111"
                     "101"
                     "101"
                     "101"
                     "111";
    case '1': return "010"
                     "110"
                     "010"
                     "010"
                     "111";
    case '2': return "111"
                     "001"
                     "111"
                     "100"
                     "111";
    case '3': return "111"
                     "001"
                     "111"
                     "001"
                     "111";
    case '4': return "101"
                     "101"
                     "111"
                     "001"
                     "001";
    case '5': return "111"
                     "100"
                     "111"
                     "001"
                     "111";
    case '6': return "111"
                     "100"
                     "111"
                     "101"
                     "111";
    case '7': return "111"
                     "001"
                     "010"
                     "010"
                     "010";
    case '8': return "111"
                     "101"
                     "111"
                     "101"
                     "111";
    case '9': return "111"
                     "101"
                     "111"
                     "001"
                     "111";
    case '-': return "000"
                     "000"
                     "111"
                     "000"
                     "000";
    case '+': return "000"
                     "010"
                     "111"
                     "010"
                     "000";
    case '.': return "000"
                     "000"
                     "000"
                     "000"
                     "010";
    case 'e':
    case 'E': return "111"
                     "100"
                     "111"
                     "100"
                     "111";
    default: return "000"
                    "000"
                    "000"
                    "000"
                    "000";
    }
}

static void plotFillRect(TGraphicCanvas &canvas, int x0, int y0, int w, int h, TGraphicColor color)
{
    int x1 = std::max(0, x0);
    int y1 = std::max(0, y0);
    int x2 = std::min(int(canvas.size.x), x0 + w);
    int y2 = std::min(int(canvas.size.y), y0 + h);
    for (int y = y1; y < y2; ++y)
        for (int x = x1; x < x2; ++x)
            canvas.setPixel(x, y, color);
}

static void plotThickPoint(TGraphicCanvas &canvas, int x, int y, int thickness, TGraphicColor color)
{
    int radius = std::max(0, thickness/2);
    plotFillRect(canvas, x - radius, y - radius, 2*radius + 1, 2*radius + 1, color);
}

static void plotThickLine(TGraphicCanvas &canvas, int x0, int y0, int x1, int y1,
                          int thickness, TGraphicColor color)
{
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        plotThickPoint(canvas, x0, y0, thickness, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2*err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void plotThickRect(TGraphicCanvas &canvas, int x, int y, int w, int h,
                          int thickness, TGraphicColor color)
{
    plotThickLine(canvas, x, y, x + w - 1, y, thickness, color);
    plotThickLine(canvas, x, y + h - 1, x + w - 1, y + h - 1, thickness, color);
    plotThickLine(canvas, x, y, x, y + h - 1, thickness, color);
    plotThickLine(canvas, x + w - 1, y, x + w - 1, y + h - 1, thickness, color);
}

static void drawText(TGraphicCanvas &canvas, int x0, int y0, const char *text,
                     int scale, TGraphicColor color, TGraphicColor background)
{
    plotFillRect(canvas, x0 - scale, y0 - scale,
                 textWidth(text, scale) + 2*scale, 5*scale + 2*scale, background);

    int x = x0;
    for (size_t i = 0; text[i] != 0; ++i)
    {
        const char *bits = glyph(text[i]);
        for (int py = 0; py < 5; ++py)
            for (int px = 0; px < 3; ++px)
                if (bits[py*3 + px] == '1')
                    plotFillRect(canvas, x + px*scale, y0 + py*scale, scale, scale, color);
        x += 4*scale;
    }
}

static double niceStep(double raw)
{
    if (!std::isfinite(raw) || raw <= 0.0)
        return 1.0;
    double base = std::pow(10.0, std::floor(std::log10(raw)));
    double frac = raw/base;
    double nice = 10.0;
    if (frac <= 1.0)
        nice = 1.0;
    else if (frac <= 2.0)
        nice = 2.0;
    else if (frac <= 5.0)
        nice = 5.0;
    return nice*base;
}

static void formatTick(double value, char *out, size_t outSize)
{
    double rounded = std::fabs(value) < 1e-12 ? 0.0 : value;
    std::snprintf(out, outSize, "%.6g", rounded);
}

class TFormulaPlotGraphicView : public TGraphicView
{
public:
    PlotRange range;
    std::vector<PlotFormula> formulas;
    std::function<void(const PlotRange &)> rangeChanged;

    TFormulaPlotGraphicView(const TRect &bounds) :
        TGraphicView(bounds, fillGraphic)
    {
        growMode = gfGrowHiX | gfGrowHiY;
        eventMask |= evMouseWheel;
    }

    void setPlot(PlotRange aRange, std::vector<PlotFormula> aFormulas)
    {
        range = aRange;
        formulas = std::move(aFormulas);
        TGraphicRuntime::invalidate(this);
        drawView();
    }

    void notifyRangeChanged()
    {
        TGraphicRuntime::invalidate(this);
        drawView();
        if (rangeChanged)
            rangeChanged(range);
    }

    void panByCells(TPoint delta)
    {
        double xSpan = range.xMax - range.xMin;
        double ySpan = range.yMax - range.yMin;
        double dx = -double(delta.x)/double(std::max(1, int(size.x)))*xSpan;
        double dy = double(delta.y)/double(std::max(1, int(size.y)))*ySpan;
        range.xMin += dx;
        range.xMax += dx;
        range.yMin += dy;
        range.yMax += dy;
        notifyRangeChanged();
    }

    void zoomAt(TPoint mouse, double factor)
    {
        double tx = double(std::max(0, std::min(int(size.x) - 1, int(mouse.x))))/
            double(std::max(1, int(size.x) - 1));
        double ty = double(std::max(0, std::min(int(size.y) - 1, int(mouse.y))))/
            double(std::max(1, int(size.y) - 1));
        double cx = range.xMin + tx*(range.xMax - range.xMin);
        double cy = range.yMax - ty*(range.yMax - range.yMin);

        range.xMin = cx - (cx - range.xMin)*factor;
        range.xMax = cx + (range.xMax - cx)*factor;
        range.yMin = cy - (cy - range.yMin)*factor;
        range.yMax = cy + (range.yMax - cy)*factor;
        notifyRangeChanged();
    }

    int mapX(double x, int left, int width) const
    {
        double t = (x - range.xMin)/(range.xMax - range.xMin);
        return left + int(t*double(std::max(1, width - 1)) + 0.5);
    }

    int mapY(double y, int top, int height) const
    {
        double t = (range.yMax - y)/(range.yMax - range.yMin);
        return top + int(t*double(std::max(1, height - 1)) + 0.5);
    }

    void drawGrid(TGraphicCanvas &canvas, int left, int top, int width, int height,
                  int scale)
    {
        TGraphicColor grid(222, 222, 222);
        TGraphicColor axis(0, 0, 0);
        TGraphicColor text(35, 35, 35);
        TGraphicColor white(255, 255, 255);

        double xStep = niceStep((range.xMax - range.xMin)/8.0);
        double yStep = niceStep((range.yMax - range.yMin)/6.0);

        int tickCount = 0;
        for (double x = std::ceil(range.xMin/xStep)*xStep;
             x <= range.xMax + xStep*0.5 && tickCount < 200;
             x += xStep, ++tickCount)
        {
            int px = mapX(x, left, width);
            canvas.line(px, top, px, top + height - 1, grid);
            plotThickLine(canvas, px, top + height - 1, px, top + height + 6, 3, axis);

            char label[32];
            formatTick(x, label, sizeof(label));
            int tw = textWidth(label, scale);
            drawText(canvas, px - tw/2, top + height + 8, label, scale, text, white);
        }

        tickCount = 0;
        for (double y = std::ceil(range.yMin/yStep)*yStep;
             y <= range.yMax + yStep*0.5 && tickCount < 200;
             y += yStep, ++tickCount)
        {
            int py = mapY(y, top, height);
            canvas.line(left, py, left + width - 1, py, grid);
            plotThickLine(canvas, left - 7, py, left, py, 3, axis);

            char label[32];
            formatTick(y, label, sizeof(label));
            int tw = textWidth(label, scale);
            drawText(canvas, left - tw - 10, py - (5*scale)/2, label, scale, text, white);
        }

        if (range.xMin <= 0.0 && range.xMax >= 0.0)
        {
            int px = mapX(0.0, left, width);
            plotThickLine(canvas, px, top, px, top + height - 1, 3, axis);
        }
        if (range.yMin <= 0.0 && range.yMax >= 0.0)
        {
            int py = mapY(0.0, top, height);
            plotThickLine(canvas, left, py, left + width - 1, py, 3, axis);
        }
        plotThickRect(canvas, left, top, width, height, 3, axis);
    }

    void drawFormula(TGraphicCanvas &canvas, const PlotFormula &formula,
                     int left, int top, int width, int height)
    {
        bool havePrevious = false;
        double previousY = 0.0;
        int previousPx = 0;
        int previousPy = 0;
        double ySpan = range.yMax - range.yMin;

        for (int px = left; px < left + width; ++px)
        {
            double t = double(px - left)/double(std::max(1, width - 1));
            double x = range.xMin + t*(range.xMax - range.xMin);
            double y = formula.expression->eval(x);
            bool visible = std::isfinite(y) && y >= range.yMin && y <= range.yMax;
            if (!visible)
            {
                havePrevious = false;
                continue;
            }

            int py = mapY(y, top, height);
            if (havePrevious && std::fabs(y - previousY) <= ySpan*0.25)
                plotThickLine(canvas, previousPx, previousPy, px, py, 3, formula.color);
            else
                plotThickPoint(canvas, px, py, 3, formula.color);

            previousY = y;
            previousPx = px;
            previousPy = py;
            havePrevious = true;
        }
    }

    virtual void paintGraphic(TGraphicCanvas &canvas)
    {
        canvas.clear(TGraphicColor(255, 255, 255));
        int scale = canvas.size.x >= 420 && canvas.size.y >= 220 ? 4 : 2;
        int left = scale == 4 ? 146 : 78;
        int bottom = scale == 4 ? 48 : 34;
        int top = 12;
        int right = 12;
        int width = std::max(1, int(canvas.size.x) - left - right);
        int height = std::max(1, int(canvas.size.y) - top - bottom);
        if (width < 20 || height < 20)
            return;

        drawGrid(canvas, left, top, width, height, scale);
        for (const PlotFormula &formula : formulas)
            drawFormula(canvas, formula, left, top, width, height);
    }

    virtual void handleEvent(TEvent &event)
    {
        if (event.what == evMouseWheel && mouseInView(event.mouse.where))
        {
            TPoint mouse = makeLocal(event.mouse.where);
            if ((event.mouse.wheel & mwUp) != 0)
                zoomAt(mouse, 0.97);
            else if ((event.mouse.wheel & mwDown) != 0)
                zoomAt(mouse, 1.031);
            clearEvent(event);
            return;
        }

        if (event.what == evMouseDown && mouseInView(event.mouse.where) &&
            event.mouse.buttons != 0)
        {
            TPoint last = makeLocal(event.mouse.where);
            while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseWheel))
            {
                TPoint now = makeLocal(event.mouse.where);
                TPoint delta {short(now.x - last.x), short(now.y - last.y)};
                if (delta.x != 0 || delta.y != 0)
                {
                    panByCells(delta);
                    last = now;
                }
            }
            clearEvent(event);
            return;
        }

        TGraphicView::handleEvent(event);
    }

    virtual int graphicMaxColors(const TGraphicProfile &) const noexcept
    {
        return 256;
    }
};

static std::string inputText(TInputLine *input)
{
    std::vector<char> data(input->dataSize());
    input->getData(data.data());
    return std::string(data.data());
}

static void setInputText(TInputLine *input, const char *text)
{
    std::vector<char> data(input->dataSize(), 0);
    std::strncpy(data.data(), text, data.size() - 1);
    input->setData(data.data());
}

static bool parseDoubleField(TInputLine *input, const char *name, double &value)
{
    std::string text = inputText(input);
    char *end = 0;
    value = std::strtod(text.c_str(), &end);
    while (end != 0 && *end != 0 && std::isspace((unsigned char) *end))
        ++end;
    if (end == text.c_str() || (end != 0 && *end != 0) || !std::isfinite(value))
    {
        messageBox(mfError | mfOKButton, "Invalid %s value.", name);
        return false;
    }
    return true;
}

class TFormulaPlotWindow : public TWindow
{
public:
    TFormulaPlotGraphicView *plot {0};
    TStaticText *formulaLabel {0};
    TInputLine *formulaInput {0};
    TStaticText *xLabel {0};
    TStaticText *xToLabel {0};
    TStaticText *yLabel {0};
    TStaticText *yToLabel {0};
    TButton *resetButton {0};
    TButton *updateButton {0};
    TInputLine *xMinInput {0};
    TInputLine *xMaxInput {0};
    TInputLine *yMinInput {0};
    TInputLine *yMaxInput {0};

    TFormulaPlotWindow() :
        TWindowInit(TWindow::initFrame),
        TWindow(TRect(5, 3, 91, 32), "Formula Plotter", wnNoNumber)
    {
        options |= ofTileable;
        plot = new TFormulaPlotGraphicView(plotBounds());
        plot->rangeChanged = [this] (const PlotRange &newRange) {
            syncRangeInputs(newRange);
        };
        insert(plot);

        formulaLabel = new TStaticText(formulaLabelBounds(), "f(x)=");
        formulaInput = new TInputLine(formulaInputBounds(), 96);
        insert(formulaLabel);
        insert(formulaInput);

        xLabel = new TStaticText(xLabelBounds(), "x from");
        xToLabel = new TStaticText(xToLabelBounds(), "to");
        yLabel = new TStaticText(yLabelBounds(), "y from");
        yToLabel = new TStaticText(yToLabelBounds(), "to");
        xMinInput = new TInputLine(xMinBounds(), 24);
        xMaxInput = new TInputLine(xMaxBounds(), 24);
        yMinInput = new TInputLine(yMinBounds(), 24);
        yMaxInput = new TInputLine(yMaxBounds(), 24);
        resetButton = new TButton(resetButtonBounds(), "~R~eset",
                                  cmResetFormulaPlot, bfNormal);
        updateButton = new TButton(updateButtonBounds(), "~U~pdate",
                                   cmUpdateFormulaPlot, bfDefault);

        insert(xLabel);
        insert(xMinInput);
        insert(xToLabel);
        insert(xMaxInput);
        insert(yLabel);
        insert(yMinInput);
        insert(yToLabel);
        insert(yMaxInput);
        insert(resetButton);
        insert(updateButton);

        setInputText(formulaInput, "sin(x^2)");
        setInputText(xMinInput, "-4");
        setInputText(xMaxInput, "4");
        setInputText(yMinInput, "-2");
        setInputText(yMaxInput, "2");
        applyInputs();
    }

    int controlsTop() const
    {
        return std::max(2, int(size.y) - 4);
    }

    TRect plotBounds() const
    {
        return TRect(1, 1, std::max(2, int(size.x) - 1),
                     std::max(2, controlsTop() - 1));
    }

    TRect formulaLabelBounds() const
    {
        int y = controlsTop() - 1;
        return TRect(2, y, 8, y + 1);
    }

    TRect formulaInputBounds() const
    {
        int y = controlsTop() - 1;
        return TRect(9, y, std::max(10, int(size.x) - 2), y + 1);
    }

    int rangeRow() const
    {
        return controlsTop();
    }

    int yRangeRow() const
    {
        return controlsTop() + 1;
    }

    TRect xLabelBounds() const
    {
        return TRect(2, rangeRow(), 8, rangeRow() + 1);
    }

    TRect xMinBounds() const
    {
        return TRect(9, rangeRow(), std::min(23, std::max(10, int(size.x) - 2)), rangeRow() + 1);
    }

    TRect xToLabelBounds() const
    {
        return TRect(25, rangeRow(), 28, rangeRow() + 1);
    }

    TRect xMaxBounds() const
    {
        return TRect(29, rangeRow(), std::min(43, std::max(30, int(size.x) - 2)), rangeRow() + 1);
    }

    TRect yLabelBounds() const
    {
        return TRect(2, yRangeRow(), 8, yRangeRow() + 1);
    }

    TRect yMinBounds() const
    {
        return TRect(9, yRangeRow(), std::min(23, std::max(10, int(size.x) - 2)), yRangeRow() + 1);
    }

    TRect yToLabelBounds() const
    {
        return TRect(25, yRangeRow(), 28, yRangeRow() + 1);
    }

    TRect yMaxBounds() const
    {
        return TRect(29, yRangeRow(), std::min(43, std::max(30, int(size.x) - 2)), yRangeRow() + 1);
    }

    TRect resetButtonBounds() const
    {
        int y = yRangeRow();
        int left = 45;
        int right = std::min(left + 11, std::max(left + 1, int(size.x) - 2));
        return TRect(left, y, right, y + 2);
    }

    TRect updateButtonBounds() const
    {
        int y = yRangeRow();
        int left = 58;
        int right = std::min(left + 12, std::max(left + 1, int(size.x) - 2));
        return TRect(left, y, right, y + 2);
    }

    void syncRangeInputs(const PlotRange &newRange)
    {
        char value[32];
        formatTick(newRange.xMin, value, sizeof(value));
        setInputText(xMinInput, value);
        formatTick(newRange.xMax, value, sizeof(value));
        setInputText(xMaxInput, value);
        formatTick(newRange.yMin, value, sizeof(value));
        setInputText(yMinInput, value);
        formatTick(newRange.yMax, value, sizeof(value));
        setInputText(yMaxInput, value);
    }

    void applyInputs()
    {
        PlotRange nextRange;
        if (!parseDoubleField(xMinInput, "x minimum", nextRange.xMin) ||
            !parseDoubleField(xMaxInput, "x maximum", nextRange.xMax) ||
            !parseDoubleField(yMinInput, "y minimum", nextRange.yMin) ||
            !parseDoubleField(yMaxInput, "y maximum", nextRange.yMax))
            return;

        if (nextRange.xMin >= nextRange.xMax || nextRange.yMin >= nextRange.yMax)
        {
            messageBox(mfError | mfOKButton,
                       "Axis ranges must have the minimum smaller than the maximum.");
            return;
        }

        std::vector<PlotFormula> nextFormulas;
        std::string source = inputText(formulaInput);
        if (source.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            messageBox(mfError | mfOKButton, "Enter a formula.");
            return;
        }

        std::string error;
        FormulaParser parser(source);
        std::unique_ptr<FormulaNode> expression = parser.parse(error);
        if (!error.empty())
        {
            messageBox(mfError | mfOKButton, "Formula f(x): %s", error.c_str());
            return;
        }

        PlotFormula formula;
        formula.text = source;
        formula.expression = std::move(expression);
        formula.color = TGraphicColor(0, 0, 0);
        nextFormulas.push_back(std::move(formula));

        plot->setPlot(nextRange, std::move(nextFormulas));
    }

    void resetAxes()
    {
        setInputText(xMinInput, "-5");
        setInputText(xMaxInput, "5");
        setInputText(yMinInput, "-5");
        setInputText(yMaxInput, "5");
        applyInputs();
    }

    virtual void changeBounds(const TRect &bounds)
    {
        TWindow::changeBounds(bounds);
        if (plot != 0)
            plot->changeBounds(plotBounds());
        if (formulaLabel != 0)
            formulaLabel->changeBounds(formulaLabelBounds());
        if (formulaInput != 0)
            formulaInput->changeBounds(formulaInputBounds());
        if (xLabel != 0) xLabel->changeBounds(xLabelBounds());
        if (xMinInput != 0) xMinInput->changeBounds(xMinBounds());
        if (xToLabel != 0) xToLabel->changeBounds(xToLabelBounds());
        if (xMaxInput != 0) xMaxInput->changeBounds(xMaxBounds());
        if (yLabel != 0) yLabel->changeBounds(yLabelBounds());
        if (yMinInput != 0) yMinInput->changeBounds(yMinBounds());
        if (yToLabel != 0) yToLabel->changeBounds(yToLabelBounds());
        if (yMaxInput != 0) yMaxInput->changeBounds(yMaxBounds());
        if (resetButton != 0) resetButton->changeBounds(resetButtonBounds());
        if (updateButton != 0) updateButton->changeBounds(updateButtonBounds());
        if (plot != 0)
            TGraphicRuntime::invalidate(plot);
    }

    virtual void sizeLimits(TPoint &min, TPoint &max)
    {
        TWindow::sizeLimits(min, max);
        if (min.x < 72)
            min.x = 72;
        if (min.y < 13)
            min.y = 13;
    }

    virtual TPalette &getPalette() const
    {
        static TPalette palette(cpGrayDialog, sizeof(cpGrayDialog) - 1);
        return palette;
    }

    bool mouseInPlot(TEvent &event) const
    {
        return plot != 0 && plot->mouseInView(event.mouse.where);
    }

    void handlePlotDrag(TEvent &event)
    {
        TPoint last = plot->makeLocal(event.mouse.where);
        while (mouseEvent(event, evMouseMove | evMouseAuto | evMouseWheel))
        {
            TPoint now = plot->makeLocal(event.mouse.where);
            TPoint delta {short(now.x - last.x), short(now.y - last.y)};
            if (delta.x != 0 || delta.y != 0)
            {
                plot->panByCells(delta);
                last = now;
            }
        }
        clearEvent(event);
    }

    virtual void handleEvent(TEvent &event)
    {
        if (event.what == evMouseDown && mouseInPlot(event) && event.mouse.buttons != 0)
        {
            handlePlotDrag(event);
            return;
        }

        TWindow::handleEvent(event);
        if (event.what == evCommand && event.message.command == cmUpdateFormulaPlot)
        {
            applyInputs();
            clearEvent(event);
        }
        else if (event.what == evCommand && event.message.command == cmResetFormulaPlot)
        {
            resetAxes();
            clearEvent(event);
        }
    }
};

} // namespace

TWindow *createFormulaPlotWindow()
{
    return new TFormulaPlotWindow();
}
