from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.enum.text import PP_ALIGN
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE

def add_title_background(slide):
    # Dark modern background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(10, 12, 16)

    # Giant Neon Cyan Circle Top Right
    c1 = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(7.5), Inches(-2), Inches(5), Inches(5))
    c1.fill.solid()
    c1.fill.fore_color.rgb = RGBColor(0, 255, 213) # Neon Cyan
    c1.line.fill.background()

    # Giant Neon Violet Circle Bottom Left
    c2 = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(-2), Inches(3.5), Inches(4), Inches(4))
    c2.fill.solid()
    c2.fill.fore_color.rgb = RGBColor(138, 43, 226) # Neon Violet
    c2.line.fill.background()
    
    # Pink accent dot
    c3 = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(8.5), Inches(4.5), Inches(0.8), Inches(0.8))
    c3.fill.solid()
    c3.fill.fore_color.rgb = RGBColor(255, 0, 128) # Neon Pink
    c3.line.fill.background()

    # Angled dynamic line
    rect = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(-1), Inches(2.8), Inches(12), Inches(0.05))
    rect.fill.solid()
    rect.fill.fore_color.rgb = RGBColor(0, 255, 213)
    rect.line.fill.background()
    rect.rotation = -15.0

def create_title_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_title_background(slide)

    # Title Box
    txBox = slide.shapes.add_textbox(Inches(0.8), Inches(0.8), Inches(8.4), Inches(2))
    tf = txBox.text_frame
    tf.word_wrap = True
    
    p = tf.paragraphs[0]
    p.text = "GPU-Accelerated Conflict Detection & Dependency Graph Construction"
    p.font.bold = True
    p.font.size = Pt(44)
    p.font.color.rgb = RGBColor(255, 255, 255)

    p2 = tf.add_paragraph()
    p2.text = "For Parallel Smart Contract Execution Using CUDA"
    p2.font.bold = True
    p2.font.size = Pt(24)
    p2.font.color.rgb = RGBColor(0, 255, 213)
    p2.space_before = Pt(10)

    # Team Box
    txBox2 = slide.shapes.add_textbox(Inches(0.8), Inches(3.5), Inches(8.4), Inches(1.5))
    tf2 = txBox2.text_frame
    tf2.word_wrap = True
    
    p3 = tf2.paragraphs[0]
    p3.text = "TEAM MEMBERS"
    p3.font.bold = True
    p3.font.size = Pt(14)
    p3.font.color.rgb = RGBColor(255, 0, 128) # Pink
    
    p4 = tf2.add_paragraph()
    p4.text = "Aditya Raj (241IT006)       |  Antony Thaikadavil (241IT010)\nNikhil Soni (241IT048)      |  Sucheth K Katte (241IT078)"
    p4.font.size = Pt(18)
    p4.font.color.rgb = RGBColor(220, 220, 220)
    p4.space_before = Pt(5)
    
    p5 = tf2.add_paragraph()
    p5.text = "Department of Information Technology, NITK Surathkal"
    p5.font.size = Pt(14)
    p5.font.color.rgb = RGBColor(150, 150, 150)
    p5.space_before = Pt(15)

def add_content_slide(prs, title, bullets):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(15, 17, 21)

    # Accent Sidebar
    bar = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), Inches(0.15), Inches(5.625))
    bar.fill.solid()
    bar.fill.fore_color.rgb = RGBColor(0, 255, 213)
    bar.line.fill.background()

    # Top Right Decorative Circle
    c1 = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(8.5), Inches(-1.5), Inches(3), Inches(3))
    c1.fill.solid()
    c1.fill.fore_color.rgb = RGBColor(138, 43, 226)
    c1.line.fill.background()
    
    # Bottom Right Decorative Dots
    c2 = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(9), Inches(5), Inches(0.3), Inches(0.3))
    c2.fill.solid()
    c2.fill.fore_color.rgb = RGBColor(255, 0, 128)
    c2.line.fill.background()

    # Title Box
    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(0.3), Inches(8), Inches(0.8))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.bold = True
    p.font.size = Pt(32)
    p.font.color.rgb = RGBColor(255, 255, 255)
    
    # Sub-line under title
    rect = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.5), Inches(1.1), Inches(4), Inches(0.02))
    rect.fill.solid()
    rect.fill.fore_color.rgb = RGBColor(138, 43, 226)
    rect.line.fill.background()
    
    # Content Box
    txBox_content = slide.shapes.add_textbox(Inches(0.5), Inches(1.4), Inches(8.5), Inches(4.0))
    tf_c = txBox_content.text_frame
    tf_c.word_wrap = True
    
    for i, bullet in enumerate(bullets):
        if i == 0:
            p_c = tf_c.paragraphs[0]
        else:
            p_c = tf_c.add_paragraph()
            
        p_c.text = "✦  " + bullet
        p_c.font.size = Pt(22)
        p_c.font.color.rgb = RGBColor(210, 215, 225)
        p_c.space_after = Pt(18)
        
def create_thank_you_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_title_background(slide) # Reuse the explosive background

    txBox = slide.shapes.add_textbox(Inches(2), Inches(2), Inches(6), Inches(2))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = "THANK YOU"
    p.font.bold = True
    p.font.size = Pt(64)
    p.font.color.rgb = RGBColor(0, 255, 213)
    p.alignment = PP_ALIGN.CENTER
    
    p2 = tf.add_paragraph()
    p2.text = "Open for Questions & Discussion"
    p2.font.size = Pt(24)
    p2.font.color.rgb = RGBColor(255, 255, 255)
    p2.alignment = PP_ALIGN.CENTER
    p2.space_before = Pt(20)

def main():
    prs = Presentation()
    prs.slide_width = Inches(10)
    prs.slide_height = Inches(5.625)

    # Page 1: Title Slide
    create_title_slide(prs)

    # Page 2: Content 1
    add_content_slide(prs, "Problem Statement & Motivation", [
        "Sequential Bottleneck: Modern blockchains (like Ethereum) execute sequentially, strictly capping throughput.",
        "Preprocessing Crisis: Before parallel execution, conflict detection and dependency graph construction are mandatory.",
        "Massive Scale: For blocks containing 10,000–100,000 transactions, this preprocessing becomes a severe computational bottleneck.",
        "Current Flaws: Existing CPU-based schedulers (e.g., Block-STM) scale extremely poorly with increasing block sizes."
    ])

    # Page 3: Content 2
    add_content_slide(prs, "The GPU Acceleration Opportunity", [
        "Massively Parallel Computing: GPUs possess the architecture required to process tens of thousands of transactions concurrently.",
        "Transaction Independence: Within massive blocks, thousands of transactions (like transfers or separate DEX swaps) are completely independent.",
        "Conflict Mechanics: Two transactions conflict only if one's write set intersects the other's read/write set.",
        "CUDA Advantage: By offloading this pairwise intersection checking to CUDA threads, we can bypass CPU speed limits."
    ])

    # Page 4: Content 3
    add_content_slide(prs, "Proposed Architecture Pipeline", [
        "Phase 1 & 2 (Extraction): Thousands of CUDA threads simultaneously extract read/write sets from realistic blockchain workloads.",
        "Phase 3 (Conflict Detection): GPU threads rapidly identify Write-Read and Write-Write intersections to discover conflicts.",
        "Phase 4 & 5 (Graph Construction): Building optimized sparse dependency graphs where Nodes = Txs and Edges = Dependencies.",
        "Phase 6 (Parallel Scheduling): Finding entirely independent execution groups (batches) using parallel topological sorting."
    ])

    # Page 5: Content 4
    add_content_slide(prs, "Algorithmic & CUDA Challenges", [
        "Avoiding O(n²): Implementing highly efficient GPU Hash indexing to avoid naive all-pairs comparisons.",
        "Irregular Memory Access: Optimizing GPU memory reads/writes via Compressed Sparse Row (CSR) and parallel vectors.",
        "Handling Workload Skew: Balancing CUDA thread blocks when popular accounts generate disproportionately massive conflict densities.",
        "Data Structures: Heavy utilization of Bitmaps, Prefix Sums, and Hash Tables custom-built for CUDA architecture."
    ])

    # Page 6: Content 5
    add_content_slide(prs, "Expected Deliverables & Evaluation", [
        "The Core Engine: A complete CUDA-accelerated conflict detector, graph constructor, and parallel scheduler.",
        "Comprehensive Benchmarks: Direct empirical comparison between Sequential CPU, Multi-threaded CPU (OpenMP), and CUDA GPU.",
        "Key Metrics: Measuring conflict detection time, graph construction speed, memory utilization, and scheduling latency.",
        "Broader Impact: Quantifying exactly where GPU acceleration benefits future parallel smart contract execution engines."
    ])

    # Page 7: Thank You Slide
    create_thank_you_slide(prs)

    prs.save('Elite_Project_Presentation.pptx')
    print("Presentation saved as Elite_Project_Presentation.pptx")

if __name__ == '__main__':
    main()
