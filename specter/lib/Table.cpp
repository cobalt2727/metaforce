#include "Specter/Table.hpp"
#include "Specter/ViewResources.hpp"
#include "Specter/RootView.hpp"

namespace Specter
{
static LogVisor::LogModule Log("Specter::Table");
#define ROW_HEIGHT 18
#define CELL_MARGIN 1

Table::Table(ViewResources& res, View& parentView, ITableDataBinding* data, ITableStateBinding* state, size_t maxColumns)
: View(res, parentView), m_data(data), m_state(state), m_maxColumns(maxColumns),
  m_hVerts(new SolidShaderVert[maxColumns * 6]), m_rowsView(*this, res)
{
    if (!maxColumns)
        Log.report(LogVisor::FatalError, "0-column tables not supported");

    m_hVertsBuf = res.m_factory->newDynamicBuffer(boo::BufferUse::Vertex, sizeof(SolidShaderVert), maxColumns * 6);

    if (!res.m_viewRes.m_texVtxFmt)
    {
        boo::VertexElementDescriptor vdescs[] =
        {
            {m_hVertsBuf, nullptr, boo::VertexSemantic::Position4},
            {m_hVertsBuf, nullptr, boo::VertexSemantic::Color}
        };
        m_hVtxFmt = res.m_factory->newVertexFormat(2, vdescs);
        boo::IGraphicsBuffer* bufs[] = {m_viewVertBlockBuf};
        m_hShaderBinding = res.m_factory->newShaderDataBinding(res.m_viewRes.m_solidShader,
                                                               m_hVtxFmt, m_hVertsBuf, nullptr,
                                                               nullptr, 1, bufs, 0, nullptr);
    }
    else
    {
        boo::IGraphicsBuffer* bufs[] = {m_viewVertBlockBuf};
        m_hShaderBinding = res.m_factory->newShaderDataBinding(res.m_viewRes.m_solidShader,
                                                               res.m_viewRes.m_texVtxFmt,
                                                               m_hVertsBuf, nullptr,
                                                               nullptr, 1, bufs, 0, nullptr);
    }
    commitResources(res);

    m_scroll.m_view.reset(new ScrollView(res, *this, ScrollView::Style::ThinIndicator));
    m_scroll.m_view->setContentView(&m_rowsView);
    updateData();
}

Table::RowsView::RowsView(Table& t, ViewResources& res)
: View(res, t), m_t(t), m_verts(new SolidShaderVert[SPECTER_TABLE_MAX_ROWS * t.m_maxColumns * 6])
{
    m_vertsBuf = res.m_factory->newDynamicBuffer(boo::BufferUse::Vertex, sizeof(SolidShaderVert),
                                                 SPECTER_TABLE_MAX_ROWS * t.m_maxColumns * 6);

    if (!res.m_viewRes.m_texVtxFmt)
    {
        boo::VertexElementDescriptor vdescs[] =
        {
            {m_vertsBuf, nullptr, boo::VertexSemantic::Position4},
            {m_vertsBuf, nullptr, boo::VertexSemantic::Color}
        };
        m_vtxFmt = res.m_factory->newVertexFormat(2, vdescs);
        boo::IGraphicsBuffer* bufs[] = {m_viewVertBlockBuf};
        m_shaderBinding = res.m_factory->newShaderDataBinding(res.m_viewRes.m_solidShader,
                                                              m_vtxFmt, m_vertsBuf, nullptr,
                                                              nullptr, 1, bufs, 0, nullptr);
    }
    else
    {
        boo::IGraphicsBuffer* bufs[] = {m_viewVertBlockBuf};
        m_shaderBinding = res.m_factory->newShaderDataBinding(res.m_viewRes.m_solidShader,
                                                              res.m_viewRes.m_texVtxFmt,
                                                              m_vertsBuf, nullptr,
                                                              nullptr, 1, bufs, 0, nullptr);
    }
    commitResources(res);
}

Table::CellView::CellView(Table& t, ViewResources& res, size_t c, size_t r)
: View(res, t), m_t(t), m_text(new TextView(res, *this, res.m_mainFont)), m_c(c), m_r(r)  {}

void Table::_setHeaderVerts(const boo::SWindowRect& sub)
{;
    if (m_headerViews.empty())
        return;
    SolidShaderVert* v = m_hVerts.get();
    const ThemeData& theme = rootView().themeData();

    float pf = rootView().viewRes().pixelFactor();
    int div = sub.size[0] / m_headerViews.size();
    int margin = CELL_MARGIN * pf;
    int rowHeight = ROW_HEIGHT * pf;
    int xOff = 0;
    int yOff = sub.size[1];
    size_t c;
    auto it = m_headerViews.cbegin();

    size_t sCol = -1;
    SortDirection sDir = SortDirection::None;
    if (m_state)
        sDir = m_state->getSort(sCol);

    for (c=0 ; c<std::min(m_maxColumns, m_columns) ; ++c)
    {
        const ViewChild<std::unique_ptr<CellView>>& hv = *it;
        const Zeus::CColor* c1 = &theme.button1Inactive();
        const Zeus::CColor* c2 = &theme.button2Inactive();
        if (hv.m_mouseDown && hv.m_mouseIn)
        {
            c1 = &theme.button1Press();
            c2 = &theme.button2Press();
        }
        else if (hv.m_mouseIn)
        {
            c1 = &theme.button1Hover();
            c2 = &theme.button2Hover();
        }

        Zeus::CColor cm1 = *c1;
        Zeus::CColor cm2 = *c2;
        if (sCol == c)
        {
            if (sDir == SortDirection::Ascending)
            {
                cm1 *= Zeus::CColor::skGreen;
                cm2 *= Zeus::CColor::skGreen;
            }
            else if (sDir == SortDirection::Descending)
            {
                cm1 *= Zeus::CColor::skRed;
                cm2 *= Zeus::CColor::skRed;
            }
        }

        v[0].m_pos.assign(xOff + margin, yOff - margin, 0);
        v[0].m_color = cm1;
        v[1] = v[0];
        v[2].m_pos.assign(xOff + margin, yOff - margin - rowHeight, 0);
        v[2].m_color = cm2;
        v[3].m_pos.assign(xOff + div - margin, yOff - margin, 0);
        v[3].m_color = cm1;
        v[4].m_pos.assign(xOff + div - margin, yOff - margin - rowHeight, 0);
        v[4].m_color = cm2;
        v[5] = v[4];
        v += 6;
        xOff += div;
        ++it;
    }

    if (c)
        m_hVertsBuf->load(m_hVerts.get(), sizeof(SolidShaderVert) * 6 * c);

    m_headerNeedsUpdate = false;
}

void Table::RowsView::_setRowVerts(const boo::SWindowRect& sub, const boo::SWindowRect& scissor)
{
    SolidShaderVert* v = m_verts.get();
    const ThemeData& theme = rootView().themeData();

    if (m_t.m_cellViews.empty())
        return;

    float pf = rootView().viewRes().pixelFactor();
    int div = sub.size[0] / m_t.m_cellViews.size();
    int spacing = (ROW_HEIGHT + CELL_MARGIN * 2) * pf;
    int margin = CELL_MARGIN * pf;
    int rowHeight = ROW_HEIGHT * pf;
    int yOff = 0;
    int idx = 0;
    while (sub.location[1] + yOff < scissor.location[1] + scissor.size[1] || (idx & 1) != 0)
    {
        yOff += spacing;
        ++idx;
    }
    int startIdx = int(m_t.m_rows) - idx;

    size_t r, c;
    for (r=0, c=0 ; r<SPECTER_TABLE_MAX_ROWS && (sub.location[1] + yOff + spacing) >= scissor.location[1] ; ++r)
    {
        const Zeus::CColor& color = (startIdx+r==m_t.m_selectedRow) ? theme.tableCellBgSelected() :
                                    ((r&1) ? theme.tableCellBg1() : theme.tableCellBg2());
        int xOff = 0;
        for (c=0 ; c<std::min(m_t.m_maxColumns, m_t.m_columns) ; ++c)
        {
            v[0].m_pos.assign(xOff + margin, yOff - margin, 0);
            v[0].m_color = color;
            v[1] = v[0];
            v[2].m_pos.assign(xOff + margin, yOff - margin - rowHeight, 0);
            v[2].m_color = color;
            v[3].m_pos.assign(xOff + div - margin, yOff - margin, 0);
            v[3].m_color = color;
            v[4].m_pos.assign(xOff + div - margin, yOff - margin - rowHeight, 0);
            v[4].m_color = color;
            v[5] = v[4];
            v += 6;
            xOff += div;
        }
        yOff -= spacing;
    }
    m_visibleStart = std::max(0, startIdx);
    m_visibleRows = r;
    if (r * c)
        m_vertsBuf->load(m_verts.get(), sizeof(SolidShaderVert) * 6 * r * c);
}

void Table::cycleSortColumn(size_t c)
{
    if (c >= m_columns)
        Log.report(LogVisor::FatalError, "cycleSortColumn out of bounds (%" PRISize ", %" PRISize ")",
                   c, m_columns);
    if (m_state)
    {
        size_t cIdx;
        SortDirection dir = m_state->getSort(cIdx);
        if (dir == SortDirection::None || cIdx != c)
            m_state->setSort(c, SortDirection::Ascending);
        else if (dir == SortDirection::Ascending)
            m_state->setSort(c, SortDirection::Descending);
        else if (dir == SortDirection::Descending)
            m_state->setSort(c, SortDirection::Ascending);
    }
}

void Table::selectRow(size_t r)
{
    if (r >= m_rows && r != -1)
        Log.report(LogVisor::FatalError, "selectRow out of bounds (%" PRISize ", %" PRISize ")",
                   r, m_rows);
    if (r == m_selectedRow)
        return;
    if (m_selectedRow != -1)
        for (auto& col : m_cellViews)
        {
            ViewChild<std::unique_ptr<CellView>>& cv = col.at(m_selectedRow);
            if (cv.m_view)
                cv.m_view->deselect();
        }
    m_selectedRow = r;
    if (m_selectedRow != -1)
        for (auto& col : m_cellViews)
        {
            ViewChild<std::unique_ptr<CellView>>& cv = col.at(m_selectedRow);
            if (cv.m_view)
                cv.m_view->select();
        }
    updateSize();
    if (m_state)
        m_state->setSelectedRow(r);
}

void Table::setMultiplyColor(const Zeus::CColor& color)
{
    View::setMultiplyColor(color);
    if (m_scroll.m_view)
        m_scroll.m_view->setMultiplyColor(color);
    for (ViewChild<std::unique_ptr<CellView>>& hv : m_headerViews)
        if (hv.m_view)
            hv.m_view->m_text->setMultiplyColor(color);
    for (auto& col : m_cellViews)
    {
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
            if (cv.m_view)
                cv.m_view->m_text->setMultiplyColor(color);
    }
}

void Table::CellView::select()
{
    m_selected = true;
    m_text->colorGlyphs(rootView().themeData().fieldText());
}

void Table::CellView::deselect()
{
    m_selected = false;
    m_text->colorGlyphs(rootView().themeData().uiText());
}

void Table::mouseDown(const boo::SWindowCoord& coord, boo::EMouseButton button, boo::EModifierKey mod)
{
    m_scroll.mouseDown(coord, button, mod);
    if (m_headerNeedsUpdate)
        _setHeaderVerts(subRect());
}


void Table::RowsView::mouseDown(const boo::SWindowCoord& coord, boo::EMouseButton button, boo::EModifierKey mod)
{
    for (ViewChild<std::unique_ptr<CellView>>& hv : m_t.m_headerViews)
        if (hv.mouseDown(coord, button, mod))
            return; /* Trap header event */
    for (std::vector<ViewChild<std::unique_ptr<CellView>>>& col : m_t.m_cellViews)
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
            cv.mouseDown(coord, button, mod);
}

void Table::CellView::mouseDown(const boo::SWindowCoord& coord, boo::EMouseButton button, boo::EModifierKey mod)
{
    if (m_r != -1)
    {
        m_t.selectRow(m_r);
        if (m_t.m_clickFrames < 15 && m_t.m_state)
            m_t.m_state->rowActivated(m_r);
        else
            m_t.m_clickFrames = 0;
    }
    else
        m_t.m_headerNeedsUpdate = true;
}

void Table::mouseUp(const boo::SWindowCoord& coord, boo::EMouseButton button, boo::EModifierKey mod)
{
    m_scroll.mouseUp(coord, button, mod);
    if (m_headerNeedsUpdate)
        _setHeaderVerts(subRect());
}

void Table::RowsView::mouseUp(const boo::SWindowCoord& coord, boo::EMouseButton button, boo::EModifierKey mod)
{
    size_t idx = 0;
    for (ViewChild<std::unique_ptr<CellView>>& hv : m_t.m_headerViews)
    {
        if (hv.m_mouseDown && hv.m_mouseIn)
            m_t.cycleSortColumn(idx);
        hv.mouseUp(coord, button, mod);
        ++idx;
    }
    for (std::vector<ViewChild<std::unique_ptr<CellView>>>& col : m_t.m_cellViews)
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
            cv.mouseUp(coord, button, mod);
}

void Table::CellView::mouseUp(const boo::SWindowCoord& coord, boo::EMouseButton button, boo::EModifierKey mod)
{
    if (m_r == -1)
        m_t.m_headerNeedsUpdate = true;
}

void Table::mouseMove(const boo::SWindowCoord& coord)
{
    m_scroll.mouseMove(coord);
    if (m_headerNeedsUpdate)
        _setHeaderVerts(subRect());
}

void Table::RowsView::mouseMove(const boo::SWindowCoord& coord)
{
    for (ViewChild<std::unique_ptr<CellView>>& hv : m_t.m_headerViews)
        hv.mouseMove(coord);
    for (std::vector<ViewChild<std::unique_ptr<CellView>>>& col : m_t.m_cellViews)
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
            cv.mouseMove(coord);
}

void Table::mouseEnter(const boo::SWindowCoord& coord)
{
    m_scroll.mouseEnter(coord);
    if (m_headerNeedsUpdate)
        _setHeaderVerts(subRect());
}

void Table::CellView::mouseEnter(const boo::SWindowCoord& coord)
{
    if (m_r == -1)
        m_t.m_headerNeedsUpdate = true;
}

void Table::mouseLeave(const boo::SWindowCoord& coord)
{
    m_scroll.mouseLeave(coord);
    if (m_headerNeedsUpdate)
        _setHeaderVerts(subRect());
}

void Table::RowsView::mouseLeave(const boo::SWindowCoord& coord)
{
    for (ViewChild<std::unique_ptr<CellView>>& hv : m_t.m_headerViews)
        hv.mouseLeave(coord);
    for (std::vector<ViewChild<std::unique_ptr<CellView>>>& col : m_t.m_cellViews)
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
            cv.mouseLeave(coord);
}

void Table::CellView::mouseLeave(const boo::SWindowCoord& coord)
{
    if (m_r == -1)
        m_t.m_headerNeedsUpdate = true;
}

void Table::scroll(const boo::SWindowCoord& coord, const boo::SScrollDelta& scroll)
{
    m_scroll.scroll(coord, scroll);
}

void Table::think()
{
    if (m_scroll.m_view)
        m_scroll.m_view->think();
    ++m_clickFrames;
}

void Table::updateData()
{
    m_header = false;
    bool newViewChildren = false;
    if (m_columns != m_data->columnCount())
        newViewChildren = true;

    m_rows = m_data->rowCount();
    m_columns = m_data->columnCount();
    if (!m_columns)
        return;

    if (newViewChildren)
    {
        m_headerViews.clear();
        m_cellViews.clear();
        m_headerViews.reserve(m_columns);
        m_cellViews.reserve(m_columns);
        for (size_t c=0 ; c<m_columns ; ++c)
        {
            m_headerViews.emplace_back();
            m_cellViews.emplace_back();
        }
    }

    ViewResources& res = rootView().viewRes();
    const Zeus::CColor& textColor = rootView().themeData().uiText();
    for (size_t c=0 ; c<m_columns ; ++c)
    {
        const std::string* headerText = m_data->header(c);
        if (headerText)
        {
            m_header = true;
            CellView* cv = new CellView(*this, res, c, -1);
            m_headerViews[c].m_view.reset(cv);
            cv->m_text->typesetGlyphs(*headerText, textColor);
        }
        else
            m_headerViews[c].m_view.reset();

        std::vector<ViewChild<std::unique_ptr<CellView>>>& col = m_cellViews[c];
        col.clear();
        col.reserve(m_rows);
        for (size_t r=0 ; r<m_rows ; ++r)
        {
            const std::string* cellText = m_data->cell(c, r);
            if (cellText)
            {
                CellView* cv = new CellView(*this, res, c, r);
                col.emplace_back();
                col.back().m_view.reset(cv);
                cv->m_text->typesetGlyphs(*cellText, textColor);
            }
            else
                col.emplace_back();
        }
    }

    updateSize();
}

void Table::resized(const boo::SWindowRect& root, const boo::SWindowRect& sub)
{
    View::resized(root, sub);
    if (m_scroll.m_view)
        m_scroll.m_view->resized(root, sub);

    float pf = rootView().viewRes().pixelFactor();
    boo::SWindowRect cell = sub;
    cell.size[1] = ROW_HEIGHT * pf;
    cell.location[1] += sub.size[1] - cell.size[1];
    int div = sub.size[0] / m_cellViews.size();
    cell.size[0] = div;

    _setHeaderVerts(sub);
    for (ViewChild<std::unique_ptr<CellView>>& hv : m_headerViews)
    {
        if (hv.m_view)
            hv.m_view->resized(root, cell);
        cell.location[0] += div;
    }
}

int Table::RowsView::nominalHeight() const
{
    float pf = rootView().viewRes().pixelFactor();
    int rows = m_t.m_rows;
    if (m_t.m_header)
        rows += 1;
    return rows * (ROW_HEIGHT + CELL_MARGIN * 2) * pf;
}

void Table::RowsView::resized(const boo::SWindowRect& root, const boo::SWindowRect& sub,
                              const boo::SWindowRect& scissor)
{
    View::resized(root, sub);
    _setRowVerts(sub, scissor);

    if (m_t.m_cellViews.empty())
        return;

    float pf = rootView().viewRes().pixelFactor();
    int div = sub.size[0] / m_t.m_cellViews.size();
    boo::SWindowRect cell = sub;
    cell.size[0] = div;
    cell.size[1] = ROW_HEIGHT * pf;
    cell.location[1] += sub.size[1] - cell.size[1];
    int spacing = (ROW_HEIGHT + CELL_MARGIN * 2) * pf;
    int hStart = cell.location[1];
    for (auto& col : m_t.m_cellViews)
    {
        cell.location[1] = hStart;
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
        {
            cell.location[1] -= spacing;
            if (cv.m_view)
                cv.m_view->resized(root, cell);
        }
        cell.location[0] += div;
    }

    m_scissorRect = scissor;
    m_scissorRect.size[1] -= spacing;
}

void Table::CellView::resized(const boo::SWindowRect& root, const boo::SWindowRect& sub)
{
    View::resized(root, sub);
    boo::SWindowRect textRect = sub;
    float pf = rootView().viewRes().pixelFactor();
    textRect.location[0] += 5 * pf;
    textRect.location[1] += 5 * pf;
    m_text->resized(root, textRect);
}

void Table::draw(boo::IGraphicsCommandQueue* gfxQ)
{
    if (m_scroll.m_view)
        m_scroll.m_view->draw(gfxQ);
}

void Table::RowsView::draw(boo::IGraphicsCommandQueue* gfxQ)
{
    gfxQ->setShaderDataBinding(m_shaderBinding);
    gfxQ->setDrawPrimitive(boo::Primitive::TriStrips);

    gfxQ->setScissor(m_scissorRect);
    gfxQ->draw(1, m_visibleRows * m_t.m_columns * 6 - 2);
    for (auto& col : m_t.m_cellViews)
    {
        size_t idx = 0;
        for (ViewChild<std::unique_ptr<CellView>>& cv : col)
        {
            if (cv.m_view && idx >= m_visibleStart && idx < m_visibleStart + m_visibleRows)
                cv.m_view->draw(gfxQ);
            ++idx;
        }
    }
    gfxQ->setScissor(rootView().subRect());

    if (m_t.m_header)
    {
        gfxQ->setShaderDataBinding(m_t.m_hShaderBinding);
        gfxQ->setDrawPrimitive(boo::Primitive::TriStrips);
        gfxQ->draw(1, m_t.m_columns * 6 - 2);
        for (ViewChild<std::unique_ptr<CellView>>& hv : m_t.m_headerViews)
            if (hv.m_view)
                hv.m_view->draw(gfxQ);
    }
}

void Table::CellView::draw(boo::IGraphicsCommandQueue* gfxQ)
{
    m_text->draw(gfxQ);
}

}
