from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.enum.text import PP_ALIGN
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE

def create_title_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Pure white background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(255, 255, 255)

    # Large Top Block for Title
    header = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), Inches(10), Inches(3.5))
    header.fill.solid()
    header.fill.fore_color.rgb = RGBColor(15, 23, 42) # Deep Slate Navy
    header.line.fill.background()

    # Accent Line
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(3.5), Inches(10), Inches(0.08))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(14, 165, 233) # Professional Blue Accent
    line.line.fill.background()

    # Title Box
    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(0.8), Inches(9), Inches(2.0))
    tf = txBox.text_frame
    tf.word_wrap = True
    
    p = tf.paragraphs[0]
    p.text = "GPU-Accelerated Conflict Detection & Dependency Graph Construction"
    p.font.name = "Arial"
    p.font.bold = True
    p.font.size = Pt(36)
    p.font.color.rgb = RGBColor(255, 255, 255)
    
    p2 = tf.add_paragraph()
    p2.text = "For Parallel Smart Contract Execution Using CUDA"
    p2.font.name = "Arial"
    p2.font.size = Pt(22)
    p2.font.color.rgb = RGBColor(186, 230, 253) # Light Blue
    p2.space_before = Pt(15)

    # Team Info Box
    txBox2 = slide.shapes.add_textbox(Inches(0.5), Inches(4.0), Inches(9), Inches(1.5))
    tf2 = txBox2.text_frame
    tf2.word_wrap = True
    
    p3 = tf2.paragraphs[0]
    p3.text = "Aditya Raj (241IT006)   |   Antony Thaikadavil (241IT010)\nNikhil Soni (241IT048)   |   Sucheth K Katte (241IT078)"
    p3.font.name = "Arial"
    p3.font.size = Pt(18)
    p3.font.color.rgb = RGBColor(55, 65, 81) # Dark Gray
    
    p4 = tf2.add_paragraph()
    p4.text = "Department of Information Technology, NITK Surathkal"
    p4.font.name = "Arial"
    p4.font.italic = True
    p4.font.size = Pt(16)
    p4.font.color.rgb = RGBColor(107, 114, 128)
    p4.space_before = Pt(10)

def add_content_slide(prs, title, bullets):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Pure white background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(255, 255, 255)

    # Header Bar
    header = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), Inches(10), Inches(1.0))
    header.fill.solid()
    header.fill.fore_color.rgb = RGBColor(15, 23, 42) # Deep Slate Navy
    header.line.fill.background()

    # Accent Line
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(1.0), Inches(10), Inches(0.04))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(14, 165, 233) # Professional Blue Accent
    line.line.fill.background()

    # Title Box (Inside Header)
    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(0.15), Inches(9), Inches(0.8))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = title
    p.font.name = "Arial"
    p.font.bold = True
    p.font.size = Pt(28)
    p.font.color.rgb = RGBColor(255, 255, 255)

    # Content Box
    txBox_content = slide.shapes.add_textbox(Inches(0.5), Inches(1.2), Inches(9.0), Inches(4.3))
    tf_c = txBox_content.text_frame
    tf_c.word_wrap = True
    
    for i, bullet in enumerate(bullets):
        if i == 0:
            p_c = tf_c.paragraphs[0]
        else:
            p_c = tf_c.add_paragraph()
            
        p_c.text = "•  " + bullet
        p_c.font.name = "Arial"
        p_c.font.size = Pt(18)
        p_c.font.color.rgb = RGBColor(31, 41, 55) # Very readable dark text
        p_c.space_after = Pt(14)

def create_thank_you_slide(prs):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    
    # Deep Slate Navy Background
    fill = slide.background.fill
    fill.solid()
    fill.fore_color.rgb = RGBColor(15, 23, 42) 

    # Accent Line
    line = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(0), Inches(3.5), Inches(10), Inches(0.08))
    line.fill.solid()
    line.fill.fore_color.rgb = RGBColor(14, 165, 233) 
    line.line.fill.background()

    txBox = slide.shapes.add_textbox(Inches(0.5), Inches(1.5), Inches(9), Inches(2))
    tf = txBox.text_frame
    p = tf.paragraphs[0]
    p.text = "Thank You"
    p.font.name = "Arial"
    p.font.bold = True
    p.font.size = Pt(54)
    p.font.color.rgb = RGBColor(255, 255, 255)
    p.alignment = PP_ALIGN.CENTER
    
    p2 = tf.add_paragraph()
    p2.text = "Open for Questions & Discussion"
    p2.font.name = "Arial"
    p2.font.size = Pt(24)
    p2.font.color.rgb = RGBColor(186, 230, 253)
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
        "The Sequential Bottleneck: Modern blockchains like Ethereum execute sequentially, capping overall system throughput.",
        "Preprocessing Crisis: Before executing in parallel, conflict detection and dependency graph construction are absolutely required.",
        "Massive Scale: For blocks containing 10,000–100,000 transactions, this preprocessing becomes a severe computational bottleneck.",
        "Current Flaws: Existing CPU-based schedulers (e.g., Block-STM) scale poorly with massive, transaction-dense blocks."
    ])

    # Page 3: Content 2
    add_content_slide(prs, "The GPU Acceleration Opportunity", [
        "Massively Parallel Architecture: GPUs natively possess the compute structure required to process tens of thousands of transactions concurrently.",
        "Transaction Independence: Within massive blocks, thousands of operations (like independent DEX swaps) can execute completely simultaneously.",
        "Conflict Mechanics: Two transactions conflict only if one's write state intersects the other's read or write state.",
        "The CUDA Advantage: Offloading pairwise intersection checking to CUDA threads effectively bypasses CPU speed limits."
    ])

    # Page 4: Content 3
    add_content_slide(prs, "Proposed Architecture Pipeline", [
        "Phase 1 & 2 (Extraction): Thousands of CUDA threads simultaneously extract read/write state sets from realistic workloads.",
        "Phase 3 (Conflict Detection): GPU threads rapidly identify Write-Read and Write-Write intersections to discover conflicts.",
        "Phase 4 & 5 (Graph Construction): Building optimized sparse dependency graphs where Nodes = Transactions and Edges = Dependencies.",
        "Phase 6 (Parallel Scheduling): Finding completely independent execution groups using parallel topological sorting algorithms."
    ])

    # Page 5: Content 4
    add_content_slide(prs, "Algorithmic & CUDA Challenges", [
        "Avoiding O(n²) Scaling: Implementing highly efficient GPU Hash Indexing to completely bypass naive all-pairs comparisons.",
        "Irregular Memory Access: Optimizing GPU memory reads and writes utilizing Compressed Sparse Row (CSR) techniques.",
        "Handling Workload Skew: Balancing CUDA thread blocks when popular smart contracts generate disproportionately dense conflicts.",
        "Custom Data Structures: Heavy utilization of Bitmaps, Prefix Sums, and Hash Tables custom-built for CUDA."
    ])

    # Page 6: Content 5
    add_content_slide(prs, "Expected Deliverables & Evaluation", [
        "The Core Engine: A complete CUDA-accelerated conflict detector, dependency graph constructor, and parallel scheduler.",
        "Comprehensive Benchmarks: Direct empirical comparison between Sequential CPU, Multi-threaded CPU (OpenMP), and CUDA GPU.",
        "Key Metrics: Measuring conflict detection time, graph construction speed, memory utilization, and scheduling latency.",
        "Broader Impact: Quantifying exactly how GPU acceleration can drastically improve future smart contract execution engines."
    ])

    # Page 7: Thank You Slide
    create_thank_you_slide(prs)

    prs.save('Final_Project_Presentation_V2.pptx')
    print("Presentation saved as Final_Project_Presentation_V2.pptx")

if __name__ == '__main__':
    main()
