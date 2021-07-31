struct Cell
{
    uint glyphIndex;
    uint2 color;
};

cbuffer ConstantBuffer : register(b0)
{
    uint2 cellSize;
    uint2 cellCount;
};
StructuredBuffer<Cell> cells : register(t0);
Texture2D<float4> glyphs : register(t1);

float3 getColor(uint i)
{
    uint r = i & 0xff;
    uint g = (i >> 8) & 0xff;
    uint b = i >> 16;
    return float3(r, g, b) / 255.0;
}

float4 main(float4 pos: SV_POSITION): SV_TARGET
{
    uint2 cellIndex = pos.xy / cellSize;
    uint2 cellPos = pos.xy % cellSize;

    Cell cell = cells[cellIndex.y * cellCount.x + cellIndex.x];

    uint2 glyphPos = uint2(cell.glyphIndex, 0);
    uint2 pixelPos = glyphPos + cellPos;
    float3 alpha = glyphs[pixelPos].xyz;

    float3 colorFg = getColor(cell.color.x);
    float3 colorBg = getColor(cell.color.y);
    float3 color = lerp(colorBg, colorFg, alpha);

    return float4(color, 1);
}
