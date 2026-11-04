from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.enum.text import PP_ALIGN
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE

def add_aesthetic_background(slide):
    # Soft off-white background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(248, 249, 250)

    # Subtle background geometric shape (light grey/beige so it doesn't hurt readability)
    # This adds depth without overlapping issues
    bg_circle = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(6.5), Inches(-1.5), Inches(5), Inches(5))
    bg_circle.fill.solid()
    bg_circle.fill.fore_color.rgb = RGBColor(238, 240, 242)
    bg_circle.line.fill.background() 

    # Elegant side bar (Dusty Blue)
    bar = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), Inches(0.15), Inches(5.625))
    bar.fill.solid()
    bar.fill.fore_color.rgb = RGBColor(74, 111, 165)
    bar.line.fill.background()

    # Small aesthetic accent dot top left
    dot = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(0.4), Inches(0.4), Inches(0.15), Inches(0.15))
    dot.fill.solid()
    dot.fill.fore_color.rgb = RGBColor(220, 100, 80) # Muted Coral
    dot.line.fill.background()

def create_title_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_aesthetic_background(slide)
    
    # Title Box
    txBox = slide.shapes.add_textbox(Inches(0.8), Inches(1.0), Inches(8.5), Inches(1.8))
    tf = txBox.text_frame
    tf.word_wrap = True
    
    p = tf.paragraphs[0]
    p.text = "GPU-Accelerated Conflict Detection & Dependency Graph Construction"
    p.font.name = "Segoe UI"
    p.font.bold = True
    p.font.size = Pt(36)
    p.font.color.rgb = RGBColor(29, 45, 68) # Dark Navy
    
    p2 = tf.add_paragraph()
    p2.text = "For Parallel Smart Contract Execution Using CUDA"
    p2.font.name = "Segoe UI Light"
    p2.font.bold = False
    p2.font.size = Pt(22)
    p2.font.color.rgb = RGBColor(74, 111, 165) # Dusty Blue
    p2.space_before = Pt(10)

    # Elegant Divider Line
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.8), Inches(3.2), Inches(3), Inches(0.02))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(220, 100, 80) # Muted Coral
    line.line.fill.background()

    # Team Info Box (Perfectly aligned below)
    txBox2 = slide.shapes.add_textbox(Inches(0.8), Inches(3.6), Inches(8.5), Inches(1.5))
    tf2 = txBox2.text_frame
    tf2.word_wrap = True
    
    p3 = tf2.paragraphs[0]
    p3.text = "Aditya Raj (241IT006)   |   Antony Thaikadavil (241IT010)\nNikhil Soni (241IT048)   |   Sucheth K Katte (241IT078)"
    p3.font.name = "Calibri"
    p3.font.size = Pt(16)
    p3.font.color.rgb = RGBColor(60, 60, 60) # Dark Grey
    
    p4 = tf2.add_paragraph()
    p4.text = "Department of Information Technology, NITK Surathkal"
    p4.font.name = "Calibri"
    p4.font.italic = True
    p4.font.size = Pt(14)
    p4.font.color.rgb = RGBColor(120, 120, 120)
    p4.space_before = Pt(8)

def add_content_slide(prs, title, bullets):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_aesthetic_background(slide)
    
    # Title Box
    txBox = slide.shapes.add_textbox(Inches(0.8), Inches(0.3), Inches(8.5), Inches(0.8))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.name = "Segoe UI"
    p.font.bold = True
    p.font.size = Pt(28)
    p.font.color.rgb = RGBColor(29, 45, 68)

    # Subtle horizontal line under title
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0.8), Inches(1.1), Inches(8.4), Inches(0.01))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(200, 200, 200)
    line.line.fill.background()
    
    # Content Box
    txBox_content = slide.shapes.add_textbox(Inches(0.8), Inches(1.3), Inches(8.4), Inches(4.0))
    tf_c = txBox_content.text_frame
    tf_c.word_wrap = True
    
    for i, bullet in enumerate(bullets):
        if i == 0:
            p_c = tf_c.paragraphs[0]
        else:
            p_c = tf_c.add_paragraph()
            
        p_c.text = "▪  " + bullet
        p_c.font.name = "Calibri"
        p_c.font.size = Pt(20)
        p_c.font.color.rgb = RGBColor(50, 50, 50) # Very readable dark charcoal
        p_c.space_after = Pt(20)
        
def create_thank_you_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    add_aesthetic_background(slide)

    txBox = slide.shapes.add_textbox(Inches(2), Inches(2), Inches(6), Inches(2))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = "Thank You."
    p.font.name = "Segoe UI"
    p.font.bold = True
    p.font.size = Pt(48)
    p.font.color.rgb = RGBColor(29, 45, 68)
    p.alignment = PP_ALIGN.CENTER
    
    p2 = tf.add_paragraph()
    p2.text = "We welcome your questions and feedback."
    p2.font.name = "Segoe UI Light"
    p2.font.size = Pt(22)
    p2.font.color.rgb = RGBColor(74, 111, 165)
    p2.alignment = PP_ALIGN.CENTER
    p2.space_before = Pt(15)

def main():
    prs = Presentation()
    prs.slide_width = Inches(10)
    prs.slide_height = Inches(5.625)

    # Page 1: Title Slide
    create_title_slide(prs)

    # Page 2: Content 1
    add_content_slide(prs, "Problem Statement & Motivation", [
        "The Sequential Bottleneck: Modern blockchains (Ethereum) execute sequentially, strictly capping throughput.",
        "Preprocessing Crisis: Before parallel execution, conflict detection and dependency graph construction are mandatory.",
        "Massive Scale: For blocks containing 10,000–100,000 transactions, this preprocessing is a severe computational bottleneck.",
        "Current Flaws: Existing CPU-based schedulers (e.g., Block-STM) scale extremely poorly with increasing block sizes."
    ])

    # Page 3: Content 2
    add_content_slide(prs, "The GPU Acceleration Opportunity", [
        "Parallel Computing: GPUs possess the architecture required to process tens of thousands of transactions concurrently.",
        "Transaction Independence: Within massive blocks, thousands of transactions (like standalone transfers) are completely independent.",
        "Conflict Mechanics: Two transactions conflict only if one's write set intersects the other's read or write set.",
        "CUDA Advantage: By offloading pairwise intersection checking to CUDA threads, we can completely bypass CPU limits."
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
        "Avoiding O(n²): Implementing highly efficient GPU Hash Indexing to avoid naive, slow all-pairs comparisons.",
        "Irregular Memory Access: Optimizing GPU memory reads and writes via Compressed Sparse Row (CSR) techniques.",
        "Handling Workload Skew: Balancing CUDA thread blocks when popular accounts generate disproportionately dense conflicts.",
        "Data Structures: Heavy utilization of Bitmaps, Prefix Sums, and Hash Tables custom-built for CUDA architecture."
    ])

    # Page 6: Content 5
    add_content_slide(prs, "Expected Deliverables & Evaluation", [
        "The Core Engine: A complete CUDA-accelerated conflict detector, graph constructor, and parallel scheduler.",
        "Comprehensive Benchmarks: Direct empirical comparison between Sequential CPU, Multi-threaded CPU (OpenMP), and GPU.",
        "Key Metrics: Measuring conflict detection time, graph construction speed, memory utilization, and scheduling latency.",
        "Broader Impact: Quantifying exactly where GPU acceleration benefits future parallel smart contract execution engines."
    ])

    # Page 7: Thank You Slide
    create_thank_you_slide(prs)

    prs.save('Aesthetic_Project_Presentation.pptx')
    print("Presentation saved as Aesthetic_Project_Presentation.pptx")

if __name__ == '__main__':
    main()
