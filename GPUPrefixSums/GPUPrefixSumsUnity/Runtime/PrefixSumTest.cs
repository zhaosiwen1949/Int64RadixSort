using UnityEngine;
using GPUPrefixSums.Runtime;
using UnityEngine.Rendering;

struct Data
{
    public uint x, y, z, w;

    public uint GetElement(int index)
    {
        switch (index)
        {
            case 0:
                return x;
            case 1:
                return y;
            case 2:
                return z;
            case 3:
                return w;
            default:
                return 0;
        }
    }
}

public class PrefixSumTest : MonoBehaviour
{
    public ComputeShader m_computeShader;
    public ComputeShader m_InitCS;
    private static int count = 64;
    private static int stride = 4;
    private uint[] m_writeArray = new uint[count * stride];
    private uint[] m_readArray = new uint[count * stride];
    
    private ComputeBuffer scanIn;
    private ComputeBuffer scanOut;
    private ComputeBuffer threadBlockReduction;

    private ReduceThenScan m_rts;
    
    // Start is called once before the first execution of Update after the MonoBehaviour is created
    void Start()
    {
        if (m_computeShader == null) return;
        if (m_InitCS == null) return;
        
        for (int i = 0; i < m_writeArray.Length; i++) {
            // m_writeArray[i] = (uint)UnityEngine.Random.Range(1, 11);
            m_writeArray[i] = 1;
        }
        Debug.Log("============================================ GPUPrefixSum ===================================");
        Debug.Log(string.Join(", ", m_writeArray));
        
        m_rts = new ReduceThenScan(
            m_computeShader,
            count + 2 * stride,
            ref threadBlockReduction);
        
        scanIn = new ComputeBuffer(count, sizeof(uint) * stride);
        scanIn.SetData(m_writeArray);
        
        scanOut = new ComputeBuffer(count, sizeof(uint) * stride);
        
        var cmd = new CommandBuffer { name = "GPUSorting" };
        
        // cmd.SetComputeBufferParam(m_InitCS, 0, "Result", scanIn);
        // cmd.SetComputeIntParams(m_InitCS, "Size", count * 4);
        // cmd.DispatchCompute(m_InitCS, 0, (count * 4 + 7)/8, 1,1);
        
        // m_rts.PrefixSumExclusive(
        //     cmd,
        //     count * 4,
        //     scanIn,
        //     scanOut,
        //     threadBlockReduction);
        var num = 21;
        m_rts.PrefixSumInclusive(
            cmd,
            num,
            scanIn,
            scanOut,
            threadBlockReduction);
        Graphics.ExecuteCommandBuffer(cmd);

        num -= 1;
        int index = num / stride;
        int data_index = num % stride;
        Data[] result = new Data[1];
        scanOut.GetData(result, 0, index, 1);
        scanOut.GetData(m_readArray);
        
        Debug.Log(result[0].GetElement(data_index));
        Debug.Log(result[0].x);
        Debug.Log(result[0].y);
        Debug.Log(result[0].z);
        Debug.Log(result[0].w);
        Debug.Log(string.Join(", ", m_readArray));
        Debug.Log("============================================ GPUPrefixSum ===================================");
        scanIn.Dispose();
        scanOut.Dispose();
        threadBlockReduction?.Dispose();
    }

    // Update is called once per frame
    void Update()
    {
        
    }
}
